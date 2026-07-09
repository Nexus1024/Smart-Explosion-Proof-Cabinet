/*
 * File      : onenet.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2024
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-XX-XX     User         first version
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <packages/onenet-latest/inc/onenet.h>
#include <cJSON.h>
#include "msg.h"
#include "rfid.h"
#include "onenet.h"

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* ONENET_DEBUG */

#include <rtdbg.h>

/* OneNET MQTT topic for data upload */
#define ONENET_TOPIC_FORMAT     "$sys/%s/%s/dp/post/json"

/* Static message ID counter */
static uint32_t msg_id_counter = 0;

/**
 * @brief Build JSON data for OneNET upload
 *
 * @param msg Pointer to message data structure
 * @return char* JSON string (must be freed by caller using cJSON_free)
 */
static char *build_onenet_json(const msg_data_t *msg)
{
    cJSON *root = NULL;
    cJSON *dp = NULL;
    cJSON *user_array = NULL;
    cJSON *operation_array = NULL;
    cJSON *item_array = NULL;
    cJSON *user_point = NULL;
    cJSON *operation_point = NULL;
    cJSON *item_point = NULL;
    char *json_str = NULL;

    /* Get RFID item ID string */
    const char *item_id = rfid_get_item_id(msg->rfid_result);

    /* Get operation type string */
    const char *operation_str = (msg->operation_type == 0) ? "store" : "take";

    /* Create root object */
    root = cJSON_CreateObject();
    if (root == NULL) {
        LOG_E("Failed to create JSON root object");
        goto error;
    }

    /* Add message ID */
    cJSON_AddNumberToObject(root, "id", msg_id_counter++);

    /* Create dp object */
    dp = cJSON_CreateObject();
    if (dp == NULL) {
        LOG_E("Failed to create JSON dp object");
        goto error;
    }

    /* Create User data stream */
    user_array = cJSON_CreateArray();
    if (user_array == NULL) {
        LOG_E("Failed to create User array");
        goto error;
    }

    user_point = cJSON_CreateObject();
    if (user_point == NULL) {
        LOG_E("Failed to create User point object");
        goto error;
    }

    cJSON_AddNumberToObject(user_point, "v", msg->finger_id);
    cJSON_AddItemToArray(user_array, user_point);
    cJSON_AddItemToObject(dp, "User", user_array);

    /* Create operation data stream */
    operation_array = cJSON_CreateArray();
    if (operation_array == NULL) {
        LOG_E("Failed to create operation array");
        goto error;
    }

    operation_point = cJSON_CreateObject();
    if (operation_point == NULL) {
        LOG_E("Failed to create operation point object");
        goto error;
    }

    cJSON_AddStringToObject(operation_point, "v", operation_str);
    cJSON_AddItemToArray(operation_array, operation_point);
    cJSON_AddItemToObject(dp, "operation", operation_array);

    /* Create item data stream */
    item_array = cJSON_CreateArray();
    if (item_array == NULL) {
        LOG_E("Failed to create item array");
        goto error;
    }

    item_point = cJSON_CreateObject();
    if (item_point == NULL) {
        LOG_E("Failed to create item point object");
        goto error;
    }

    cJSON_AddStringToObject(item_point, "v", item_id);
    cJSON_AddItemToArray(item_array, item_point);
    cJSON_AddItemToObject(dp, "item", item_array);

    /* Add dp to root */
    cJSON_AddItemToObject(root, "dp", dp);

    /* Convert to JSON string */
    json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        LOG_E("Failed to convert JSON to string");
        goto error;
    }

    LOG_D("Built JSON: %s", json_str);

    /* Clean up */
    cJSON_Delete(root);
    return json_str;

error:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    return NULL;
}

/**
 * @brief Initialize OneNET MQTT connection
 *
 * @return int 0 on success, -1 on failure
 */
int onenet_init(void)
{
    int result;

    LOG_I("Initializing OneNET MQTT...");

    result = onenet_mqtt_init();
    if (result < 0) {
        LOG_E("Failed to initialize OneNET MQTT");
        return -1;
    }

    LOG_I("OneNET MQTT initialized successfully");
    return 0;
}

/**
 * @brief Upload event data to OneNET cloud
 *
 * @param msg Pointer to message data structure
 * @return int 0 on success, -1 on failure
 */
int onenet_upload_event(const msg_data_t *msg)
{
    char *json_str = NULL;
    char topic[128];
    int ret = -1;

    if (msg == NULL) {
        LOG_E("Invalid message pointer");
        return -1;
    }

    /* Build JSON data */
    json_str = build_onenet_json(msg);
    if (json_str == NULL) {
        LOG_E("Failed to build JSON data");
        return -1;
    }

    /* Build MQTT topic using macros from rtconfig.h */
    rt_snprintf(topic, sizeof(topic), ONENET_TOPIC_FORMAT,
                ONENET_INFO_PROID,    /* Product ID from rtconfig.h */
                ONENET_INFO_DEVID);   /* Device ID from rtconfig.h */

    LOG_D("Publishing to topic: %s", topic);
    LOG_D("JSON data: %s", json_str);

    /* Publish to OneNET */
    ret = onenet_mqtt_publish(topic, (uint8_t *)json_str, strlen(json_str));
    if (ret < 0) {
        LOG_E("Failed to publish data to OneNET");
    } else {
        LOG_I("Data published to OneNET successfully");
    }

    /* Free JSON string */
    cJSON_free(json_str);

    return ret;
}

/**
 * @brief Process data received from OneNET (placeholder)
 *
 * @param recv_data Received data
 * @param size Data size
 * @return int Always returns 0
 */
int onenet_port_data_process(char *recv_data, rt_size_t size)
{
    /* This function can be implemented if downstream command processing is needed */
    LOG_D("Received data from OneNET: %.*s", size, recv_data);
    return 0;
}
