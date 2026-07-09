/*
 * File      : msg.h
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2024
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-XX-XX     User         first version
 */

#ifndef __MSG_H__
#define __MSG_H__

#include <rtthread.h>
#include "finger.h"
#include "rfid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 消息类型枚举 */
typedef enum {
    MSG_TYPE_NONE = 0,   /* 无消息 */
    MSG_TYPE_STORE,      /* 存物操作 */
    MSG_TYPE_TAKE,       /* 取物操作 */
    MSG_TYPE_SCAN        /* 单纯RFID扫描 */
} msg_type_t;

/* 消息数据结构 */
typedef struct {
    msg_type_t msg_type;        /* 消息类型 */
    uint16_t finger_id;         /* 指纹ID */
    uint8_t operation_type;     /* 操作类型：0-存物，1-取物 */
    rfid_result_t rfid_result;  /* RFID扫描结果 */
} msg_data_t;

/**
 * @brief 初始化消息模块
 *
 * 该函数初始化消息模块，注册指纹和RFID回调函数，准备接收和处理事件。
 *
 * @return rt_err_t RT_EOK表示成功，其他值表示失败
 */
rt_err_t msg_module_init(void);

/**
 * @brief 消息类型转字符串
 *
 * 将消息类型枚举转换为可读的字符串，用于调试和日志输出。
 *
 * @param type 消息类型枚举值
 * @return const char* 对应的字符串描述
 */
const char* msg_type_to_str(msg_type_t type);

/**
 * @brief 处理并发送消息
 *
 * 处理给定的消息数据，并根据消息类型决定是否上传到OneNET。
 * 目前仅通过串口输出调试信息，将来可扩展为实际的上传功能。
 *
 * @param msg 指向消息数据结构的指针
 */
void msg_process_and_send(msg_data_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __MSG_H__ */
