/*
 * File      : onenet.h
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2024
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-XX-XX     User         first version
 */

#ifndef __ONENET_H__
#define __ONENET_H__

#include <rtthread.h>
#include "msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OneNET MQTT connection
 *
 * This function initializes the OneNET MQTT client and establishes connection
 * to the OneNET cloud platform.
 *
 * @return int 0 on success, -1 on failure
 */
int onenet_init(void);

/**
 * @brief Upload event data to OneNET cloud
 *
 * This function uploads fingerprint, operation, and RFID data to OneNET platform
 * in JSON format via MQTT protocol.
 *
 * @param msg Pointer to message data structure containing fingerprint ID,
 *            operation type, and RFID result
 * @return int 0 on success, -1 on failure
 */
int onenet_upload_event(const msg_data_t *msg);

/**
 * @brief Process data received from OneNET
 *
 * This function processes downstream commands or data received from OneNET cloud.
 * Currently implemented as a placeholder for future functionality.
 *
 * @param recv_data Received data buffer
 * @param size Size of received data
 * @return int Always returns 0
 */
int onenet_port_data_process(char *recv_data, rt_size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __ONENET_H__ */
