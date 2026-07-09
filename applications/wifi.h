#ifndef __WIFI_H__
#define __WIFI_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include <wlan_prot.h>
#include <wlan_cfg.h>

/* WiFi连接配置 */
#define WLAN_SSID      "your_wifi_ssid"
#define WLAN_PASSWORD  "your_wifi_password"
#define NET_READY_TIME_OUT (rt_tick_from_millisecond(15 * 1000))

/* 全局信号量，用于网络连接和扫描 */
extern struct rt_semaphore net_ready;
extern struct rt_semaphore scan_done;

/* WiFi初始化函数 */
int wifi(void);

/* 回调函数声明 */
void wlan_scan_report_hander(int event, struct rt_wlan_buff *buff, void *parameter);
void wlan_scan_done_hander(int event, struct rt_wlan_buff *buff, void *parameter);
void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter);
void wlan_station_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter);

/* 内部使用的静态函数声明 */
static void wlan_connect_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static void wlan_connect_fail_handler(int event, struct rt_wlan_buff *buff, void *parameter);
static void print_wlan_information(struct rt_wlan_info *info, int index);
static int wifi_autoconnect(void);

#endif /* __WIFI_H__ */
