/*-------------------------------------------------------------*/
/*!
 *      @file       imx708.c
 *      @date       2026/xx/xx
 *      @author     mrzm99
 *      @brief
 *      @note
 */
/*-------------------------------------------------------------*/
#include "imx708.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/videodev2.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

/*-------------------------------------------------------------*/
/*! @brief  macro
 */
#define CAMERA_DEV                  "/dev/video0"
#define BUFFER_NUM                  (4)

/*-------------------------------------------------------------*/
/*! @brief  v4l2 buff manage structure
 */
typedef struct {
    void *p_start;
    size_t length;
} v4l2_buff_t;

/*-------------------------------------------------------------*/
/*! @brief  control block
 */
typedef struct {
    bool is_open;
    int fd;
    GstElement *p_pipeline;
    GstElement *p_appsrc;
    GstElement *p_text_overlay;
    GstElement *p_appsink;
    v4l2_buff_t v4l2_buffers[BUFFER_NUM];
    uint8_t *p_raw8_buffer;
} imx708_ctl_blk_t;

static imx708_ctl_blk_t imx708_ctl_blk;
#define get_myself()        (&imx708_ctl_blk)

/*-------------------------------------------------------------*/
/*! @brief  GStreamer pipeline
 */
static const char *pipeline_str =
    "appsrc name=mysrc format=time is-live=true do-timestamp=true ! "
    "video/x-bayer,format=bggr,width=1536,height=864,framerate=10/1 ! "
    "bayer2rgb ! "
    "videobalance brightness=0.1 saturation=1.2 ! "
    "videoconvert ! "
    "textoverlay name=gnss_text text=\"Init...\" valignment=top halignment=left font-desc=\"Sans, 7\" ! "
    "videoconvert ! "
    "jpegenc quality=75 ! "
    "appsink name=mysink drop=true max-buffers=1";

/*-------------------------------------------------------------*/
/*! @brief  run command
 */
static int run_cmd(const char *p_cmd)
{
    int ret = system(p_cmd);

    if (ret == -1) {
        perror("system() failed to execute");
        return -1;
    }

    if (WIFEXITED(ret)) {
        int exit_status = WEXITSTATUS(ret);

        if (exit_status == 0) {
            return 0;
        } else {
            fprintf(stderr, "Command failed with exit code %d: %s\n", exit_status, p_cmd);
            return -1;
        }
    } else {
        fprintf(stderr, "Command terminated abnormally: %s\n", p_cmd);
        return -1;
    }

    return 0;
}

/*-------------------------------------------------------------*/
/*! @brief  init camera by media-ctl
 */
static int setup_media_pipeline()
{
    g_print("--- Setting up Media Controller ---\n");

    if (run_cmd("media-ctl -V '\"imx708\":0 [fmt:SRGGB10_1X10/1536x864]'") != 0) {
        return -1;
    }
    if (run_cmd("media-ctl -V '\"cdns_csi2rx.30101000.csi-bridge\":0 [fmt:SRGGB10_1X10/1536x864]'") != 0) {
        return -1;
    }
    if (run_cmd("media-ctl -V '\"cdns_csi2rx.30101000.csi-bridge\":1 [fmt:SRGGB10_1X10/1536x864]'") != 0) {
        return -1;
    }
    if (run_cmd("media-ctl -V '\"30102000.ticsi2rx\":0 [fmt:SRGGB10_1X10/1536x864]'") != 0 ) {
        return -1;
    }
    g_print("--- Media Controller setup complete ---\n\n");

    return 0;
}

/*-------------------------------------------------------------*/
/*! @brief  init
 */
int imx708_init()
{
    imx708_ctl_blk_t *this = get_myself();

    memset(this, 0, sizeof(imx708_ctl_blk_t));
    this->is_open = false;

    return 0;
}

/*-------------------------------------------------------------*/
/*! @brief  open
 */
int imx708_open()
{
    imx708_ctl_blk_t *this = get_myself();

    if (this->is_open) {
        return 0;
    }

    // init media controller
    if (setup_media_pipeline() != 0) {
        return -1;
    }

    //
    gst_init(NULL, NULL);

    //
    int fd = open(CAMERA_DEV, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("Cannot open device");
        return -1;
    }

    //
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = v4l2_fourcc('R', 'G', '1', '0');
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT failed");
        return -1;
    }

    // バッファの要求 (MMAP方式)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_NUM;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);

    // メモリマッピングとキューイング
    for (int i = 0; i < BUFFER_NUM; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);

        this->v4l2_buffers[i].length = buf.length;
        this->v4l2_buffers[i].p_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    // ==========================================
    // [Phase 2] GStreamerパイプライン (appsrc入り)
    // ==========================================
    GError *error = NULL;

    GstElement *pipeline = gst_parse_launch(pipeline_str, &error);
    if (error != NULL) {
        g_printerr("Failed to build pipeline: %s\n", error->message);
        return -1;
    }

    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysrc");
    GstElement *text_overlay = gst_bin_get_by_name(GST_BIN(pipeline), "gnss_text");
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // 
    size_t raw8_size = WIDTH * HEIGHT;
    uint8_t *raw8_buffer = (uint8_t *)malloc(raw8_size);
    if (raw8_buffer == NULL) {
        return -1;
    }

    // キャプチャ開始
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        return -1;
    }
    g_print("start up camera...\n");

    // save context
    this->is_open = true;
    this->fd = fd;
    this->p_pipeline = pipeline;
    this->p_appsrc = appsrc;
    this->p_text_overlay = text_overlay;
    this->p_appsink = appsink;
    this->p_raw8_buffer = raw8_buffer;

    return 0;
}

/*-------------------------------------------------------------*/
/*! @brief  get camera data
 */
int imx708_get_camera_data(uint8_t *p_buff, uint32_t *p_size, uint8_t *p_text)
{
    imx708_ctl_blk_t *this = get_myself();
    uint32_t raw8_size = HEIGHT * WIDTH;
    char gnss_str[256];
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(this->fd, &fds);
    struct timeval tv = {2, 0}; // 2秒タイムアウト

    if ((p_buff == NULL) || (p_size == NULL)) {
        return -1;
    }

    if (!this->is_open) {
        return -1;
    }

    // 
    *p_size = 0;

    // 1. カーネルからフレームが届くのを待つ
    int r = select(this->fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        g_printerr("V4L2 capture timeout!\n");
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 2. フレームをキューから引き抜く (DQBUF)
    if (ioctl(this->fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("ioctl Failed");
        return -1;
    }

    // 3. 10-bit(1ピクセル16-bit格納)を8-bitに変換
    // RG10フォーマットは通常、下位10ビットにデータが入っているため、2ビット右シフトで上位8ビットを残す
    uint16_t *src16 = (uint16_t *)this->v4l2_buffers[buf.index].p_start;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        this->p_raw8_buffer[i] = (uint8_t)(src16[i] >> 2);
    }

    // 4. 変換した8-bitデータをGStreamerに流し込む (appsrc)
    GstBuffer *gst_buf = gst_buffer_new_allocate(NULL, raw8_size, NULL);
    gst_buffer_fill(gst_buf, 0, this->p_raw8_buffer, raw8_size);
    gst_app_src_push_buffer(GST_APP_SRC(this->p_appsrc), gst_buf); // gst_bufの所有権は移る

    // 5. 使い終わったV4L2バッファをカーネルに返す (QBUF)
    ioctl(this->fd, VIDIOC_QBUF, &buf);

    // 6. テキストオーバーレイの更新
    if ((this->p_text_overlay != NULL) && (p_text != NULL)) {
        g_object_set(this->p_text_overlay, "text", p_text, NULL);
    }

    // 7. GStreamerの出口(appsink)から処理済みRGBフレームを引き抜く
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(this->p_appsink), 100 * GST_MSECOND);
    if (sample != NULL) {
        GstBuffer *out_buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(out_buffer, &map, GST_MAP_READ)) {

            if (map.size > 2 && map.data[0] == 0xFF && map.data[1] == 0xD8) {
                g_print("  -> Valid JPEG header detected (FF D8).\n");
                memcpy(p_buff, map.data, map.size);
                *p_size = map.size;
            }

            gst_buffer_unmap(out_buffer, &map);
        }
        gst_sample_unref(sample);
    } else {
//-        g_print("Frame %d: V4L2 OK, GStreamer processing...\n", frame_count);
    }

    return 0;
}

/*-------------------------------------------------------------*/
/*! @brief  close
 */
int imx708_close()
{
    imx708_ctl_blk_t *this = get_myself();

    if (!this->is_open) {
        return 0;
    }

    // 終了処理
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(this->fd, VIDIOC_STREAMOFF, &type) < 0 ) {
        return -1;
    }

    gst_element_set_state(this->p_pipeline, GST_STATE_NULL);
    gst_object_unref(this->p_pipeline);

    for (int i = 0; i < BUFFER_NUM; i++) {
        if (this->v4l2_buffers[i].p_start != NULL) {
            munmap(this->v4l2_buffers[i].p_start, this->v4l2_buffers[i].length);
        }
    }

    free(this->p_raw8_buffer);
    close(this->fd);

    this->is_open = false;

    return 0;
}
