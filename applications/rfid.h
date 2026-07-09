#ifndef __RFID_H__
#define __RFID_H__

#include <rtthread.h>

/* RFID结果枚举 */
typedef enum {
    RFID_RESULT_ITEM_1 = 0,    /* 物品1: NO.1 */
    RFID_RESULT_ITEM_2 = 1,    /* 物品2: NO.2 */
    RFID_RESULT_UNKNOWN = 2,   /* 未知物品 */
    RFID_RESULT_NONE = 3,      /* 无标签 */
    RFID_RESULT_ERROR = 4      /* 错误 */
} rfid_result_t;

/* 回调函数类型声明 */
typedef void (*rfid_callback_t)(rfid_result_t result);

/* RFID模块初始化 */
rt_err_t rfid_module_init(void);

/* RFID单次扫描 */
rfid_result_t rfid_scan_once(void);

/* 快速RFID扫描（简化版） */
rfid_result_t rfid_scan_quick(void);

/* 获取物品名称 */
const char* rfid_get_item_name(rfid_result_t result);

/* 获取物品ID */
const char* rfid_get_item_id(rfid_result_t result);

/* 注册RFID扫描回调函数 */
void rfid_register_callback(rfid_callback_t callback);

#endif /* __RFID_H__ */
