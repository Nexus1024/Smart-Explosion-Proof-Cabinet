/*
 * File      : onenet_mqtt.c
 * COPYRIGHT (C) 2006 - 2018, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-24     chenyong     first version
 * 2023-xx-xx     YourName     modified for data stream model
 */
#include <stdlib.h>
#include <string.h>
#include <string.h>

#include <cJSON_util.h>

#include <paho_mqtt.h>

#include <packages/onenet-latest/inc/onenet.h>

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet.mqtt"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* ONENET_DEBUG */

#include <rtdbg.h>

#if RTTHREAD_VERSION < 40100
#ifdef RT_USING_DFS
#include <dfs_posix.h>
#endif
#else
#ifdef RT_USING_DFS
#include <dfs_file.h>
#include <unistd.h>
#include <stdio.h>      /* rename() */
#include <sys/stat.h>
#include <sys/statfs.h> /* statfs() */
#endif
#endif

/* 数据流模型上报主题 */
#define ONENET_TOPIC_DP "$sys/" ONENET_INFO_PROID "/" ONENET_INFO_DEVID "/dp/post/json"

/* 全局消息ID，每次发送时递增 */
static uint32_t msg_id_counter = 0;

static rt_bool_t init_ok = RT_FALSE;
static MQTTClient mq_client;
struct rt_onenet_info onenet_info;

struct onenet_device
{
    struct rt_onenet_info *onenet_info;

    void(*cmd_rsp_cb)(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size);

} onenet_mqtt;

static void mqtt_callback(MQTTClient *c, MessageData *msg_data)
{
    size_t res_len = 0;
    uint8_t *response_buf = RT_NULL;
    char topicname[45] = { "$crsp/" };

    RT_ASSERT(c);
    RT_ASSERT(msg_data);

    LOG_D("topic %.*s receive a message", msg_data->topicName->lenstring.len, msg_data->topicName->lenstring.data);

    LOG_D("message length is %d", msg_data->message->payloadlen);

    if (onenet_mqtt.cmd_rsp_cb != RT_NULL)
    {
        onenet_mqtt.cmd_rsp_cb((uint8_t *) msg_data->message->payload, msg_data->message->payloadlen, &response_buf,
                &res_len);

        if (response_buf != RT_NULL || res_len != 0)
        {
            strncat(topicname, &(msg_data->topicName->lenstring.data[6]), msg_data->topicName->lenstring.len - 6);

            onenet_mqtt_publish(topicname, response_buf, strlen((const char *)response_buf));

            ONENET_FREE(response_buf);

        }

    }

}

static void mqtt_connect_callback(MQTTClient *c)
{
    LOG_D("Enter mqtt_connect_callback!");
}

static void mqtt_online_callback(MQTTClient *c)
{
    LOG_D("Enter mqtt_online_callback!");
}

static void mqtt_offline_callback(MQTTClient *c)
{
    LOG_D("Enter mqtt_offline_callback!");
}

static void mqtt_usr_callback(MQTTClient *c)
{
    LOG_D("Enter mqtt_usr_callback!");
}

static rt_err_t onenet_mqtt_entry(void)
{
    MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;

    mq_client.uri = onenet_info.server_uri;
    memcpy(&(mq_client.condata), &condata, sizeof(condata));
    mq_client.condata.clientID.cstring = onenet_info.device_id;
    mq_client.condata.keepAliveInterval = 30;
    mq_client.condata.cleansession = 1;
    mq_client.condata.username.cstring = onenet_info.pro_id;
    mq_client.condata.password.cstring = onenet_info.auth_info;

    mq_client.buf_size = mq_client.readbuf_size = 1024 * 2;
    mq_client.buf = (unsigned char *) ONENET_CALLOC(1, mq_client.buf_size);
    mq_client.readbuf = (unsigned char *) ONENET_CALLOC(1, mq_client.readbuf_size);
    if (!(mq_client.buf && mq_client.readbuf))
    {
        LOG_E("No memory for MQTT client buffer!");
        return -RT_ENOMEM;
    }

    /* registered callback */
    mq_client.connect_callback = mqtt_connect_callback;
    mq_client.online_callback = mqtt_online_callback;
    mq_client.offline_callback = mqtt_offline_callback;

    mq_client.defaultMessageHandler = mqtt_callback;

    mq_client.messageHandlers[0].topicFilter = rt_strdup(ONENET_MQTT_SUBTOPIC);
    mq_client.messageHandlers[0].callback = mqtt_usr_callback;
    mq_client.messageHandlers[0].qos = QOS1;

    paho_mqtt_start(&mq_client);

    return RT_EOK;
}

static rt_err_t onenet_get_info(void)
{
    char dev_id[ONENET_INFO_DEVID_LEN] = { 0 };
    char api_key[ONENET_INFO_APIKEY_LEN] = { 0 };
    char auth_info[ONENET_INFO_AUTH_LEN] = { 0 };

#ifdef ONENET_USING_AUTO_REGISTER
    char name[ONENET_INFO_NAME_LEN] = { 0 };

    if (!onenet_port_is_registed())
    {
        if (onenet_port_get_register_info(name, auth_info) < 0)
        {
            LOG_E("onenet get register info fail!");
            return -RT_ERROR;
        }

        if (onenet_http_register_device(name, auth_info) < 0)
        {
            LOG_E("onenet register device fail! name is %s,auth info is %s", name, auth_info);
            return -RT_ERROR;
        }
    }

    if (onenet_port_get_device_info(dev_id, api_key, auth_info))
    {
        LOG_E("onenet get device id fail,dev_id is %s,api_key is %s,auth_info is %s", dev_id, api_key, auth_info);
        return -RT_ERROR;
    }

#else
    /* 使用安全的字符串复制，避免缓冲区溢出 */
    strncpy(dev_id, ONENET_INFO_DEVID, ONENET_INFO_DEVID_LEN - 1);
    dev_id[ONENET_INFO_DEVID_LEN - 1] = '\0';

    strncpy(auth_info, ONENET_INFO_AUTH, ONENET_INFO_AUTH_LEN - 1);
    auth_info[ONENET_INFO_AUTH_LEN - 1] = '\0';
#endif

    /* 使用安全的字符串复制 */
    strncpy(onenet_info.device_id, dev_id, ONENET_INFO_DEVID_LEN - 1);
    onenet_info.device_id[ONENET_INFO_DEVID_LEN - 1] = '\0';

    strncpy(onenet_info.api_key, api_key, ONENET_INFO_APIKEY_LEN - 1);
    onenet_info.api_key[ONENET_INFO_APIKEY_LEN - 1] = '\0';

    strncpy(onenet_info.pro_id, ONENET_INFO_PROID, ONENET_INFO_PROID_LEN - 1);
    onenet_info.pro_id[ONENET_INFO_PROID_LEN - 1] = '\0';

    strncpy(onenet_info.auth_info, auth_info, ONENET_INFO_AUTH_LEN - 1);
    onenet_info.auth_info[ONENET_INFO_AUTH_LEN - 1] = '\0';

    strncpy(onenet_info.server_uri, ONENET_SERVER_URL, ONENET_INFO_URL_LEN - 1);
    onenet_info.server_uri[ONENET_INFO_URL_LEN - 1] = '\0';

    /* 调试信息，查看复制后的信息 */
    LOG_D("Device Info: dev_id=%s, pro_id=%s, auth_info_len=%d",
          onenet_info.device_id, onenet_info.pro_id, strlen(onenet_info.auth_info));
    LOG_D("Server URI: %s", onenet_info.server_uri);

    return RT_EOK;
}
/**
 * onenet mqtt client init.
 *
 * @param   NULL
 *
 * @return  0 : init success
 *         -1 : get device info fail
 *         -2 : onenet mqtt client init fail
 */
int onenet_mqtt_init(void)
{
    int result = 0;

    if (init_ok)
    {
        LOG_D("onenet mqtt already init!");
        return 0;
    }

    if (onenet_get_info() < 0)
    {
        result = -1;
        goto __exit;
    }

    onenet_mqtt.onenet_info = &onenet_info;
    onenet_mqtt.cmd_rsp_cb = RT_NULL;

    if (onenet_mqtt_entry() < 0)
    {
        result = -2;
        goto __exit;
    }

__exit:
    if (!result)
    {
        LOG_I("RT-Thread OneNET package(V%s) initialize success.", ONENET_SW_VERSION);
        init_ok = RT_TRUE;
    }
    else
    {
        LOG_E("RT-Thread OneNET package(V%s) initialize failed(%d).", ONENET_SW_VERSION, result);
    }

    return result;
}

/**
 * mqtt publish msg to topic
 *
 * @param   topic   target topic
 * @param   msg     message to be sent
 * @param   len     message length
 *
 * @return  0 : publish success
 *         -1 : publish fail
 */
rt_err_t onenet_mqtt_publish(const char *topic, const uint8_t *msg, size_t len)
{
    MQTTMessage message;

    RT_ASSERT(topic);
    RT_ASSERT(msg);

    message.qos = QOS0;
    message.retained = 0;
    message.payload = (void *) msg;
    message.payloadlen = len;

    if (MQTTPublish(&mq_client, topic, &message) < 0)
    {
        return -1;
    }

    return 0;
}

/**
 * 获取当前时间戳（秒）
 * 注意：这里使用系统启动时间，如果没有RTC，可以注释掉t字段
 */
static time_t get_timestamp(void)
{
    return (time_t)(rt_tick_get() / RT_TICK_PER_SECOND);
}

static rt_err_t onenet_mqtt_get_digit_data(const char *ds_name, const double digit, char **out_buff, size_t *length)
{
    rt_err_t result = RT_EOK;
    cJSON *root = RT_NULL;
    cJSON *dp = RT_NULL;
    cJSON *datapoint_array = RT_NULL;
    cJSON *point = RT_NULL;
    char *msg_str = RT_NULL;
    char msg_id_str[16] = {0};

    RT_ASSERT(ds_name);
    RT_ASSERT(out_buff);
    RT_ASSERT(length);

    root = cJSON_CreateObject();
    dp = cJSON_CreateObject();
    datapoint_array = cJSON_CreateArray();
    point = cJSON_CreateObject();

    if (!root || !dp || !datapoint_array || !point)
    {
        LOG_E("MQTT publish digit data failed! cJSON create object error!");
        if (root) cJSON_Delete(root);
        if (dp) cJSON_Delete(dp);
        if (datapoint_array) cJSON_Delete(datapoint_array);
        if (point) cJSON_Delete(point);
        return -RT_ENOMEM;
    }

    /* 数据流模型格式：{"id":123, "dp":{"temperature":[{"v":25.6, "t":1634567890}]}} */

    /* 生成消息ID（自增） */
    msg_id_counter++;
    snprintf(msg_id_str, sizeof(msg_id_str), "%d", msg_id_counter);

    /* 添加消息ID（使用数字类型） */
    cJSON_AddNumberToObject(root, "id", atoi(msg_id_str));

    /* 添加dp对象 */
    cJSON_AddItemToObject(root, "dp", dp);

    /* 在dp对象中添加数据点数组 */
    cJSON_AddItemToObject(dp, ds_name, datapoint_array);
    cJSON_AddItemToArray(datapoint_array, point);

    /* 添加值 */
    cJSON_AddNumberToObject(point, "v", digit);

    /* 添加时间戳（可选） */
    time_t timestamp = get_timestamp();
    cJSON_AddNumberToObject(point, "t", timestamp);

    /* 渲染cJSON结构到缓冲区 */
    msg_str = cJSON_PrintUnformatted(root);
    if (!msg_str)
    {
        LOG_E("MQTT publish digit data failed! cJSON print unformatted error return NULL!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    *out_buff = ONENET_MALLOC(strlen(msg_str) + 1);
    if (!(*out_buff))
    {
        LOG_E("ONENET mqtt upload digit data failed! No memory for send buffer!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    strncpy(*out_buff, msg_str, strlen(msg_str));
    (*out_buff)[strlen(msg_str)] = '\0';  // 确保字符串结尾
    *length = strlen(msg_str);

__exit:
    if (root)
    {
        cJSON_Delete(root);
    }
    if (msg_str)
    {
        cJSON_free(msg_str);
    }

    return result;
}

/**
 * Upload digit data to OneNET cloud.
 *
 * @param   ds_name     datastream name
 * @param   digit       digit data
 *
 * @return  0 : upload digit data success
 *         -5 : no memory
 */
rt_err_t onenet_mqtt_upload_digit(const char *ds_name, const double digit)
{
    char *send_buffer = RT_NULL;
    rt_err_t result = RT_EOK;
    size_t length = 0;

    RT_ASSERT(ds_name);

    result = onenet_mqtt_get_digit_data(ds_name, digit, &send_buffer, &length);
    if (result < 0)
    {
        goto __exit;
    }

    LOG_D("Upload digit data: %s, length: %d", send_buffer, length);
    result = onenet_mqtt_publish(ONENET_TOPIC_DP, (uint8_t *)send_buffer, length);
    if (result < 0)
    {
        LOG_E("onenet publish failed (%d)!", result);
        goto __exit;
    }

__exit:
    if (send_buffer)
    {
        ONENET_FREE(send_buffer);
    }

    return result;
}

static rt_err_t onenet_mqtt_get_string_data(const char *ds_name, const char *str, char **out_buff, size_t *length)
{
    rt_err_t result = RT_EOK;
    cJSON *root = RT_NULL;
    cJSON *dp = RT_NULL;
    cJSON *datapoint_array = RT_NULL;
    cJSON *point = RT_NULL;
    char *msg_str = RT_NULL;
    char msg_id_str[16] = {0};

    RT_ASSERT(ds_name);
    RT_ASSERT(str);
    RT_ASSERT(out_buff);
    RT_ASSERT(length);

    root = cJSON_CreateObject();
    dp = cJSON_CreateObject();
    datapoint_array = cJSON_CreateArray();
    point = cJSON_CreateObject();

    if (!root || !dp || !datapoint_array || !point)
    {
        LOG_E("MQTT publish string data failed! cJSON create object error!");
        if (root) cJSON_Delete(root);
        if (dp) cJSON_Delete(dp);
        if (datapoint_array) cJSON_Delete(datapoint_array);
        if (point) cJSON_Delete(point);
        return -RT_ENOMEM;
    }

    /* 数据流模型格式：{"id":123, "dp":{"status":[{"v":"on", "t":1634567890}]}} */

    /* 生成消息ID（自增） */
    msg_id_counter++;
    snprintf(msg_id_str, sizeof(msg_id_str), "%d", msg_id_counter);

    /* 添加消息ID（使用数字类型） */
    cJSON_AddNumberToObject(root, "id", atoi(msg_id_str));

    /* 添加dp对象 */
    cJSON_AddItemToObject(root, "dp", dp);

    /* 在dp对象中添加数据点数组 */
    cJSON_AddItemToObject(dp, ds_name, datapoint_array);
    cJSON_AddItemToArray(datapoint_array, point);

    /* 添加字符串值 */
    cJSON_AddStringToObject(point, "v", str);

    /* 添加时间戳（可选） */
    time_t timestamp = get_timestamp();
    cJSON_AddNumberToObject(point, "t", timestamp);

    /* 渲染cJSON结构到缓冲区 */
    msg_str = cJSON_PrintUnformatted(root);
    if (!msg_str)
    {
        LOG_E("MQTT publish string data failed! cJSON print unformatted error return NULL!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    *out_buff = ONENET_MALLOC(strlen(msg_str) + 1);
    if (!(*out_buff))
    {
        LOG_E("ONENET mqtt upload string data failed! No memory for send buffer!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    strncpy(*out_buff, msg_str, strlen(msg_str));
    (*out_buff)[strlen(msg_str)] = '\0';  // 确保字符串结尾
    *length = strlen(msg_str);

__exit:
    if (root)
    {
        cJSON_Delete(root);
    }
    if (msg_str)
    {
        cJSON_free(msg_str);
    }

    return result;
}

/**
 * upload string data to OneNET cloud.
 *
 * @param   ds_name     datastream name
 * @param   str         string data
 *
 * @return  0 : upload digit data success
 *         -5 : no memory
 */
rt_err_t onenet_mqtt_upload_string(const char *ds_name, const char *str)
{
    char *send_buffer = RT_NULL;
    rt_err_t result = RT_EOK;
    size_t length = 0;

    RT_ASSERT(ds_name);
    RT_ASSERT(str);

    result = onenet_mqtt_get_string_data(ds_name, str, &send_buffer, &length);
    if (result < 0)
    {
        goto __exit;
    }

    LOG_D("Upload string data: %s, length: %d", send_buffer, length);
    result = onenet_mqtt_publish(ONENET_TOPIC_DP, (uint8_t *)send_buffer, length);
    if (result < 0)
    {
        LOG_E("onenet mqtt publish string data failed!");
        goto __exit;
    }

__exit:
    if (send_buffer)
    {
        ONENET_FREE(send_buffer);
    }

    return result;
}

/**
 * set the command responses call back function
 *
 * @param   cmd_rsp_cb  command responses call back function
 *
 * @return  0 : set success
 *         -1 : function is null
 */
void onenet_set_cmd_rsp_cb(void (*cmd_rsp_cb)(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size))
{

    onenet_mqtt.cmd_rsp_cb = cmd_rsp_cb;

}

/* 注意：二进制数据上传函数需要较大修改，因为数据流模型不支持原格式
   如果需要二进制上传，建议使用Base64编码后作为字符串上传
   这里暂时保留原函数，但可能不适用于数据流模型 */

static rt_err_t onenet_mqtt_get_bin_data(const char *str, const uint8_t *bin, int binlen, uint8_t **out_buff, size_t *length)
{
    rt_err_t result = RT_EOK;
    cJSON *root = RT_NULL;
    cJSON *dp = RT_NULL;
    cJSON *datapoint_array = RT_NULL;
    cJSON *point = RT_NULL;
    char *msg_str = RT_NULL;
    char msg_id_str[16] = {0};
    char *base64_str = RT_NULL;

    RT_ASSERT(str);
    RT_ASSERT(bin);
    RT_ASSERT(out_buff);
    RT_ASSERT(length);

    /* 注意：这里需要Base64编码，由于没有现成的Base64编码函数，暂时不实现
       如果需要二进制上传，建议使用外部Base64编码库或实现编码函数 */

    LOG_W("Binary upload not fully implemented for data stream model. Need Base64 encoding.");

    /* 临时实现：将二进制数据转换为16进制字符串 */
    char *hex_str = RT_NULL;
    hex_str = (char *)ONENET_MALLOC(binlen * 2 + 1);
    if (!hex_str)
    {
        return -RT_ENOMEM;
    }

    for (int i = 0; i < binlen; i++)
    {
        sprintf(&hex_str[i * 2], "%02x", bin[i]);
    }
    hex_str[binlen * 2] = '\0';

    root = cJSON_CreateObject();
    dp = cJSON_CreateObject();
    datapoint_array = cJSON_CreateArray();
    point = cJSON_CreateObject();

    if (!root || !dp || !datapoint_array || !point)
    {
        LOG_E("MQTT publish binary data failed! cJSON create object error!");
        ONENET_FREE(hex_str);
        if (root) cJSON_Delete(root);
        if (dp) cJSON_Delete(dp);
        if (datapoint_array) cJSON_Delete(datapoint_array);
        if (point) cJSON_Delete(point);
        return -RT_ENOMEM;
    }

    /* 数据流模型格式 */
    msg_id_counter++;
    snprintf(msg_id_str, sizeof(msg_id_str), "%d", msg_id_counter);

    cJSON_AddNumberToObject(root, "id", atoi(msg_id_str));
    cJSON_AddItemToObject(root, "dp", dp);
    cJSON_AddItemToObject(dp, str, datapoint_array);
    cJSON_AddItemToArray(datapoint_array, point);
    cJSON_AddStringToObject(point, "v", hex_str);

    time_t timestamp = get_timestamp();
    cJSON_AddNumberToObject(point, "t", timestamp);

    msg_str = cJSON_PrintUnformatted(root);
    if (!msg_str)
    {
        LOG_E("MQTT publish binary data failed! cJSON print unformatted error return NULL!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    *out_buff = (uint8_t *)ONENET_MALLOC(strlen(msg_str) + 1);
    if (!(*out_buff))
    {
        LOG_E("ONENET mqtt upload binary data failed! No memory for send buffer!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    strncpy((char *)*out_buff, msg_str, strlen(msg_str));
    (*out_buff)[strlen(msg_str)] = '\0';
    *length = strlen(msg_str);

__exit:
    if (hex_str)
    {
        ONENET_FREE(hex_str);
    }
    if (root)
    {
        cJSON_Delete(root);
    }
    if (msg_str)
    {
        cJSON_free(msg_str);
    }

    return result;
}

/**
 * upload binary data to onenet cloud by path
 *
 * @param   ds_name     datastream name
 * @param   bin         binary file
 * @param   len         binary file length
 *
 * @return  0 : upload success
 *         -1 : invalid argument or open file fail
 */
rt_err_t onenet_mqtt_upload_bin(const char *ds_name, uint8_t *bin, size_t len)
{
    size_t length = 0;
    rt_err_t result = RT_EOK;
    uint8_t *send_buffer = RT_NULL;

    RT_ASSERT(ds_name);
    RT_ASSERT(bin);

    result = onenet_mqtt_get_bin_data(ds_name, bin, len, &send_buffer, &length);
    if (result < 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    result = onenet_mqtt_publish(ONENET_TOPIC_DP, send_buffer, length);
    if (result < 0)
    {
        LOG_E("onenet publish data failed(%d)!", result);
        result = -RT_ERROR;
        goto __exit;
    }

__exit:
    if (send_buffer)
    {
        ONENET_FREE(send_buffer);
    }

    return result;
}

#ifdef RT_USING_DFS
/**
 * upload binary data to onenet cloud by path
 *
 * @param   ds_name     datastream name
 * @param   bin_path    binary file path
 *
 * @return  0 : upload success
 *         -1 : invalid argument or open file fail
 */
rt_err_t onenet_mqtt_upload_bin_by_path(const char *ds_name, const char *bin_path)
{
    int fd;
    size_t length = 0, bin_size = 0;
    size_t bin_len = 0;
    struct stat file_stat;
    rt_err_t result = RT_EOK;
    uint8_t *send_buffer = RT_NULL;
    uint8_t * bin_array = RT_NULL;

    RT_ASSERT(ds_name);
    RT_ASSERT(bin_path);

    if (stat(bin_path, &file_stat) < 0)
    {
        LOG_E("get file state fail!, bin path is %s",bin_path);
        return -RT_ERROR;
    }
    else
    {
        bin_len = file_stat.st_size;
        if (bin_len > 3 * 1024 * 1024)
        {
            LOG_E("bin length must be less than 3M, %s length is %d", bin_path, bin_len);
            return -RT_ERROR;
        }

    }

    fd = open(bin_path, O_RDONLY);
    if (fd >= 0)
    {
        bin_array = (uint8_t *) ONENET_MALLOC(bin_len);

        bin_size = read(fd, bin_array, file_stat.st_size);
        close(fd);
        if (bin_size <= 0)
        {
            LOG_E("read %s file fail!", bin_path);
            result = -RT_ERROR;
            goto __exit;
        }
    }
    else
    {
        LOG_E("open %s file fail!", bin_path);
        return -RT_ERROR;
    }

    result = onenet_mqtt_get_bin_data(ds_name, bin_array, bin_size, &send_buffer, &length);
    if (result < 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    result = onenet_mqtt_publish(ONENET_TOPIC_DP, send_buffer, length);
    if (result < 0)
    {
        LOG_E("onenet publish %s data failed(%d)!", bin_path, result);
        result = -RT_ERROR;
        goto __exit;
    }

__exit:
    if (send_buffer)
    {
        ONENET_FREE(send_buffer);
    }
    if (bin_array)
    {
        ONENET_FREE(bin_array);
    }

    return result;
}
#endif /* RT_USING_DFS */

#ifdef FINSH_USING_MSH
#include <finsh.h>

MSH_CMD_EXPORT(onenet_mqtt_init, OneNET cloud mqtt initializate);

#endif
