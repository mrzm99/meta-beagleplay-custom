/*-------------------------------------------------------------*/
/*!
 *      @file       imx708.h
 *      @date       2026/xx/xx
 *      @author     mrzm99
 *      @brief
 *      @note
 */
/*-------------------------------------------------------------*/

#ifndef __IMX708_H__
#define __IMX708_H__

#include <stdint.h>

#define WIDTH               (1536)
#define HEIGHT              (864)

int imx708_init(void);
int imx708_open(void);
int imx708_get_camera_data(uint8_t *p_buff, uint32_t *p_size, uint8_t *p_text);
int imx708_close(void);

#endif
