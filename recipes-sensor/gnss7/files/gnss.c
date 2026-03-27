/*--------------------------------------------------------------*/
/*!
 *      @file       gnss.c
 *      @author     mrzm99
 *      @date       2026/xx/xx
 *      @brief      GNSS driver
 *      @note
 */
/*--------------------------------------------------------------*/

#include "gnss.h"
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define GNSS_I2C_BUS                "/dev/i2c-3"
#define GNSS_I2C_ADDR               (0x42)
#define GNSS_REG_AVAIL_BYTES        (0xFD)
#define GNSS_REG_DATA_STREAM        (0xFF)

#define RING_BUFF_MAX               (512)

/*--------------------------------------------------------------*/
/*! @brief
 */
#define is_ring_buff_empty(widx, ridx)      (((widx + 1) % RING_BUFF_MAX) != ridx)
#define get_ring_buff_empty_num(widx, ridx) ((ridx - widx - 1 + RING_BUFF_MAX) % RING_BUFF_MAX)
#define convert_utc_to_jst(utc_hour)        ((utc_hour + 9) % 24)

/*--------------------------------------------------------------*/
/*! @brief
 */
typedef struct {
    bool is_valid;          //!< 測位ステータス
    char day[10];           //!< 日
    int jst_hour;           //!< 時間(JST)
    int minute;             //!< 分
    int second;             //!< 秒
    double latitude;        //!< 緯度(生データ)
    double longitude;       //!< 経度(生データ)
} gnrmc_data_t;


struct MemoryStruct {
    char *memory;
    size_t size;
};
/*--------------------------------------------------------------*/
/*! @brief  control block
 */
typedef struct {
    bool is_open;                       //!< driver state
    int fd;                             //!< file discripter for I2C driver

    uint8_t gnss_data[RING_BUFF_MAX];   //!< ring buffer for GNSS data
    uint32_t widx;                      //!< write index
    uint32_t ridx;                      //!< read index
} gnss_drv_ctl_t;

static gnss_drv_ctl_t gnss_drv_ctl_blk;
#define get_myself()    (&gnss_drv_ctl_blk)

/*--------------------------------------------------------------*/
/*! @brief  get NMEA data
 */
static int get_nmea_data(uint8_t *p_data, uint32_t num)
{
    gnss_drv_ctl_t *this = get_myself();
    uint8_t reg;
    uint8_t read_buff[2];
    int ret;

    // check driver open
    if (!this->is_open) {
        return -1;
    }

    //
    uint32_t empty_num = get_ring_buff_empty_num(this->widx, this->ridx);

    uint32_t read_num = empty_num > num ? num : empty_num;

    //
    reg = GNSS_REG_DATA_STREAM;
    if (write(this->fd, &reg, 1) != 1) {
        perror("");
        return -1;
    }

    //
    if ((ret = read(this->fd, p_data, read_num)) <= 0) {
        perror("4");
        return -1;
    }

    return ret;
}

/*--------------------------------------------------------------*/
/*! @brief  insert GNSS data into ring buffer
 */
static int insert_gnss_data(uint8_t *p_data, uint32_t size)
{
    gnss_drv_ctl_t *this = get_myself();

    if (p_data == NULL) {
        return -1;
    }

    for (int i = 0; i < size; i++) {
        uint8_t c = p_data[i];
        if (((c <= 0x20) || (0x7E <= c)) && (c != '\r') && (c != '\n')) {
            continue;
        }

        if (is_ring_buff_empty(this->widx, this->ridx)) {
            this->gnss_data[this->widx] = c;
            this->widx = (this->widx + 1) % RING_BUFF_MAX;
        } else {
            return -1;
        }
    }

    return 0;
}

/*--------------------------------------------------------------*/
/*! @brief  get line data
 */
static uint8_t *get_nmea_line()
{
    gnss_drv_ctl_t *this = get_myself();
    bool has_line_data = false;
    uint32_t num = 0;
    uint8_t *p_line_data = NULL;

    // search line break
    int idx = this->ridx;
    while (idx != this->widx) {
        uint8_t c = this->gnss_data[idx];
        num++;
        if (c == '\n') {
            has_line_data = true;
            break;
        }
        idx = (idx + 1) % RING_BUFF_MAX;
    }

    if (!has_line_data) {
        return NULL;
    } else {
        p_line_data = (uint8_t *)malloc(num + 1);
        for (int i = 0; i < num; i++) {
            p_line_data[i] = this->gnss_data[this->ridx];
            this->ridx = (this->ridx + 1) % RING_BUFF_MAX;
        }
        p_line_data[num] = '\0';
    }

    return p_line_data;
}

/*--------------------------------------------------------------*/
/*! @brief  parse GNRMC line data
 */
static int parse_gnrmc_data(uint8_t *p_gnrmc_data, gnrmc_data_t *p_parsed_data)
{
    char*token;
    char *rest = (char*)p_gnrmc_data;
    int index = 0;

    if ((p_gnrmc_data == NULL) || (p_parsed_data == NULL)) {
        return -1;
    }

    while ((token = strsep(&rest, ",")) != NULL) {
        switch (index) {
            case 1: // time
                if (strlen(token) >= 6) {
                    char hh[3] = { token[0], token[1], '\0' };
                    char mm[3] = { token[2], token[3], '\0' };
                    char ss[3] = { token[4], token[5], '\0' };

                    int utc_hour = atoi(hh);
                    p_parsed_data->jst_hour = convert_utc_to_jst(utc_hour);
                    p_parsed_data->minute = atoi(mm);
                    p_parsed_data->second = atoi(ss);
                }
                break;

            case 2: // status
                if (token[0] == 'A') {
                    p_parsed_data->is_valid = true;
                }
                break;

            case 3: // latitude
                if (strlen(token) > 0) {
                    p_parsed_data->latitude = atof(token);
                }
                break;

            case 5: // longitude
                if (strlen(token) > 0) {
                    p_parsed_data->longitude = atof(token);
                }
                break;

            case 9: // day
                if (strlen(token) > 0) {
                    memcpy(p_parsed_data->day, token, 8);
                    p_parsed_data->day[8] = 0;
                }

            default:
                break;
        }
        index++;
    }

    return 0;
}

/*--------------------------------------------------------------*/
/*! @brief  convert NMEA to 10sinnsuu
 */
static double nmea_to_decimal(double raw_val)
{
    int degrees = (int)(raw_val / 100);
    double minutes = raw_val - (degrees * 100);
    return degrees + (minutes / 60.0);
}

/*--------------------------------------------------------------*/
/*! @brief
 */
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) return 0; // メモリ不足

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/*--------------------------------------------------------------*/
/*! @brief
 */
static char *get_address(double latidude, double longitude)
{
      // 2. OpenStreetMap APIのURL構築 (日本語で結果を取得するために accept-language=ja を指定)
    char url[256];
    char *p_address = NULL;
    snprintf(url, sizeof(url),
             "https://nominatim.openstreetmap.org/reverse?format=json&lat=%.6f&lon=%.6f&zoom=16&accept-language=en",
             latidude, longitude);

    // 3. libcurlでHTTP GETリクエストを送信
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        // Nominatim APIの規約に従い、User-Agentを必ず設定する
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "BeaglePlay-GNSS-App/1.0");

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            fprintf(stderr, "curlのエラー: %s\n", curl_easy_strerror(res));
        } else {
            // 4. cJSONでJSON文字列を解析
            cJSON *json = cJSON_Parse(chunk.memory);
            if (json != NULL) {
                cJSON *display_name = cJSON_GetObjectItemCaseSensitive(json, "display_name");
                if (display_name != NULL) {
                    p_address = malloc(256);
                    memset(p_address, 0, 256);
                    memcpy(p_address, display_name->valuestring, strlen(display_name->valuestring));
                }
                cJSON_Delete(json);
            } else {
                printf("JSONのパースに失敗しました。\n");
            }
        }
        curl_easy_cleanup(curl);
        free(chunk.memory);
    }
    curl_global_cleanup();

    return p_address;
}

/*--------------------------------------------------------------*/
/*! @brief  init
 */
int gnss_init()
{
    gnss_drv_ctl_t *this = get_myself();
    memset(this, 0, sizeof(gnss_drv_ctl_blk));

    return 0;
}

/*--------------------------------------------------------------*/
/*! @brief  open
 */
int gnss_open()
{
    gnss_drv_ctl_t *this = get_myself();

    // check driver open
    if (this->is_open) {
        return 0;
    }

    // open driver
    if ((this->fd = open(GNSS_I2C_BUS, O_RDWR)) < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }

    // set I2C slave address
    if (ioctl(this->fd, I2C_SLAVE, GNSS_I2C_ADDR) < 0) {
        perror("Failed to acquire bus access");
        return -1;
    }

    // update driver state
    this->is_open = true;

    return 0;
}

/*--------------------------------------------------------------*/
/*! @brief  close
 */
int gnss_close()
{
    gnss_drv_ctl_t *this = get_myself();

    // check driver open
    if (!this->is_open) {\
        return 0;
    }

    // close
    if (close(this->fd) != 0) {
        return -1;
    }

    // initialize context
    this->is_open = false;
    this->fd = -1;

    return 0;
}

/*--------------------------------------------------------------*/
/*! @brief  get data
 */
int gnss_get_data(gnss_data_t *p_gnss_data)
{
    gnss_drv_ctl_t *this = get_myself();
    gnrmc_data_t gnrmc_data;
    int ret;
    uint8_t buff[256];
    uint8_t *p_line_data;

    //
    if (p_gnss_data == NULL) {
        return -1;
    }

    // get GNRMC line
    while (1) {
        // get GNSS data
        if ((ret = get_nmea_data(buff, sizeof(buff))) <= 0) {
            fprintf(stderr, "Failed: get_nmea_data()\n");
        } else {
            // insert data into ring buffer
            int err = insert_gnss_data(buff, ret);
            if (err < 0) {
                fprintf(stderr, "Failed: insert_gnss_data()");
            }
        }

        // get line
        while (1) {
            p_line_data = get_nmea_line();
            if (p_line_data == NULL) {
                break;
            } else {
                if ((p_line_data[0] == '$') && (strncmp((char*)p_line_data, "$GNRMC", 6) == 0)) {
                    goto EXIT_GET_LINE;
                }
            }
        }
    }

EXIT_GET_LINE:
    // parse
    memset(&gnrmc_data, 0, sizeof(gnrmc_data));
    if ((ret = parse_gnrmc_data(p_line_data, &gnrmc_data)) != 0) {
        fprintf(stderr, "Failed: parse_gnrmc_data()\n");
        return -1;
    }

    // convert data
    double latidude, longitude;
    if ((gnrmc_data.latitude > 0) && (gnrmc_data.longitude > 0)) {
        latidude = nmea_to_decimal(gnrmc_data.latitude);
        longitude = nmea_to_decimal(gnrmc_data.longitude);
    }

    // get address
    char *address = "";
    if (gnrmc_data.is_valid) {
        address = get_address(latidude, longitude);
    }

    // save data
    memset(p_gnss_data->address, 0, sizeof(p_gnss_data->address));
    memset(p_gnss_data->time, 0, sizeof(p_gnss_data->time));

    memcpy(p_gnss_data->address, address, strlen(address));
    snprintf(p_gnss_data->time, sizeof(p_gnss_data->time), "%s %d:%d:%d", gnrmc_data.day, gnrmc_data.jst_hour, gnrmc_data.minute, gnrmc_data.second);

    //
    free(p_line_data);

    return 0;
}
