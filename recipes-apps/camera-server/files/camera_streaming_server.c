/*--------------------------------------------------------*/
/*!
 *      @file       camera streaming_server.c
 *      @date       2026/xx/xx
 *      @author     mrmz99
 *      @brief      camera streaming server app
 *      @note
 */
/*--------------------------------------------------------*/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <imx708.h>
#include <gnss.h>

/*--------------------------------------------------------*/
/*! @brief macro
 */
#define PORT_NO                     (8080)
#define MAX_JPEG_SIZE               (2 * 1024 * 1024)
#define OVERLAY_STRING_SIZE         (512)

/*--------------------------------------------------------*/
/*! @brief shared memory structure
 */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    uint32_t jpeg_size;
    uint32_t frame_count;
    uint8_t  jpeg_data[MAX_JPEG_SIZE];
    uint8_t overlay_string[OVERLAY_STRING_SIZE];
} shared_mem_t;

/*--------------------------------------------------------*/
/*! @brief streaming process
 */
static void handle_client(int client_fd, shared_mem_t *shm)
{
    // MJPEGのストリーミング用HTTPヘッダ
    const char *http_header =
        "HTTP/1.0 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=--myboundary\r\n\r\n";

    send(client_fd, http_header, strlen(http_header), 0);

    uint8_t *local_buf = malloc(MAX_JPEG_SIZE);
    uint32_t local_size = 0;
    uint32_t last_frame = 0;

    while (1) {
        // 1. 共有メモリをロックして最新フレームを待つ
        pthread_mutex_lock(&shm->mutex);

        // 新しいフレームが来るまで待機 (CPU使用率を抑えるため)
        while (shm->frame_count == last_frame) {
            pthread_cond_wait(&shm->cond, &shm->mutex);
        }

        // 2. 共有メモリからローカルに素早くコピーし、即座にロック解除
        local_size = shm->jpeg_size;
        memcpy(local_buf, shm->jpeg_data, local_size);
        last_frame = shm->frame_count;

        pthread_mutex_unlock(&shm->mutex);

        // 3. コピーしたJPEGデータをブラウザに送信
        char frame_header[256];
        snprintf(frame_header, sizeof(frame_header),
                 "--myboundary\r\n"
                 "Content-Type: image/jpeg\r\n"
                 "Content-Length: %u\r\n\r\n", local_size);

        // ヘッダ送信
        if (send(client_fd, frame_header, strlen(frame_header), 0) < 0) break;
        // JPEG本体送信
        if (send(client_fd, local_buf, local_size, 0) < 0) break;
        // フッタ送信
        if (send(client_fd, "\r\n", 2, 0) < 0) break;
    }

    free(local_buf);
    close(client_fd);
    printf("Client disconnected.\n");
}

/*--------------------------------------------------------*/
/*! @brief get GNSS data process
 */
static void run_gnss_process(shared_mem_t *shm)
{
    signal(SIGCHLD, SIG_DFL);
    gnss_init();
    if (gnss_open() != 0) {
        fprintf(stderr, "Failed to open GNSS\n");
        exit(1);
    }

    gnss_data_t gnss_data;
    printf("GNSS process started.\n");
    while (1) {
        if (gnss_get_data(&gnss_data) != 0) {
            // error
        } else {
            pthread_mutex_lock(&shm->mutex);
            snprintf((char*)shm->overlay_string, OVERLAY_STRING_SIZE - 1, "%s %s", gnss_data.time, gnss_data.address);
            pthread_mutex_unlock(&shm->mutex);
        }
        sleep(1);
    }
}

/*--------------------------------------------------------*/
/*! @brief get camera data process
 */
static void run_camera_process(shared_mem_t *shm)
{
    signal(SIGCHLD, SIG_DFL);
    imx708_init();
    if (imx708_open() != 0) {
        fprintf(stderr, "Failed to open camera\n");
        exit(1);
    }

    uint8_t *temp_buf = malloc(MAX_JPEG_SIZE);
    uint32_t size = 0;

    printf("Camera process started.\n");

    while (1) {
        char local_overlay_string[OVERLAY_STRING_SIZE];

        pthread_mutex_lock(&shm->mutex);
        strncpy(local_overlay_string, (char*)shm->overlay_string, OVERLAY_STRING_SIZE);
        local_overlay_string[OVERLAY_STRING_SIZE - 1] = '\0';
        pthread_mutex_unlock(&shm->mutex);

        imx708_get_camera_data(temp_buf, &size, (uint8_t*)local_overlay_string);

        if (size > 0) {
            // 共有メモリをロックして最新のJPEGを書き込む
            pthread_mutex_lock(&shm->mutex);
            memcpy(shm->jpeg_data, temp_buf, size);
            shm->jpeg_size = size;
            shm->frame_count++;

            // 待機中の全クライアントプロセスに「新しい画像が来たよ！」と通知
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
        }
    }
}

/*--------------------------------------------------------*/
/*! @brief main process
 */
int main()
{
    // ゾンビプロセス防止 (子プロセスが終了したら自動回収)
    signal(SIGCHLD, SIG_IGN);
    // クライアント切断時の強制終了防止
    signal(SIGPIPE, SIG_IGN);

    // 1. 共有メモリの作成 (mmapの MAP_ANONYMOUS を使うことでファイル不要でメモリ共有)
    shared_mem_t *shm = mmap(NULL, sizeof(shared_mem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // 2. プロセス間で共有できる Mutex と Cond の初期化
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex, &mattr);

    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shm->cond, &cattr);

    // 3. カメラ専用のプロセスをフォーク
    pid_t cam_pid = fork();
    if (cam_pid == 0) {
        run_camera_process(shm);
        exit(0);
    }

    // GNSSセンサ用プロセス
    pid_t gnss_pid = fork();
    if (gnss_pid == 0) {
        run_gnss_process(shm);
        exit(0);
    }

    // 4. 親プロセスは TCP サーバになる
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_NO);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("Streaming server listening on port %d...\n", PORT_NO);

    // 5. クライアントからの接続待ちループ
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        printf("New client connected!\n");

        // クライアントごとにプロセスをフォーク
        if (fork() == 0) {
            close(server_fd); // 子プロセスはリスニングソケット不要
            handle_client(client_fd, shm);
            exit(0);
        }
        close(client_fd); // 親プロセスはクライアントソケット不要
    }

    return 0;
}
