/*--------------------------------------------------------------*/
/*!
 *      @file       gnss.h
 *      @author     mrzm99
 *      @date       2026/xx/xx
 *      @brief      GNSS driver
 *      @note
 */
/*--------------------------------------------------------------*/

#ifndef __GNNS_H__
#define __GNNS_H__

#include <stdint.h>

typedef struct {
    char address[256];
    char time[128];
} gnss_data_t;

int gnss_init(void);
int gnss_open(void);
int gnss_close(void);
int gnss_get_data(gnss_data_t *p_gnss_data);

#endif
