#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_http_client.h"

#include "driver/gpio.h"
#include "cjson.h"

#include "led_strip.h"

// 33:00
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_standard_types.h>

#include <app_wifi.h>

// GPIO assignment
#define LED_STRIP_BLINK_GPIO  1
// Numbers of the LED in the strip
#define LED_STRIP_LED_NUMBERS 16
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

// #define GPIO_INPUT_PIN GPIO_NUM_9 // 废弃Reboot按键
#define MAX_HTTP_OUTPUT_BUFFER 2048

static const char *TAG = "HTTP_CLIENT";

static bool shouldFetchWeather  = false;

// 19:53
static int temp = 0; //温度
static char *name; //地区：深圳
static char *text; //天气：中雨
static char *wind_class; //风力：2级
static int rh; //湿度：91

// 23:25
static uint8_t base_r = 150;
static uint8_t base_g = 150;
static uint8_t base_b = 150;

static int led_mode = 0;

static int direction = 1;
static int brightness = 0;

// 信号量
SemaphoreHandle_t data_ready;
SemaphoreHandle_t json_done;

// 33:49
esp_rmaker_device_t *temp_sensor_device;
esp_rmaker_device_t *led_control;


/**
 * @brief 报告数据
 *
 * 此函数用于报告温度、天气、地区、风力和湿度等传感器数据，并根据特定条件触发警告。
 *
 * @note 该函数是静态的，只能在当前文件内部调用。
 */
static void data_report() {
    ESP_LOGI(TAG, "data_report...");

    // 更新并报告温度参数
    // esp_rmaker_param_update_and_report(
    //         esp_rmaker_device_get_param_by_type(temp_sensor_device, "温度"),
    //         esp_rmaker_str(temp));

    // // 更新并报告天气参数
    // esp_rmaker_param_update_and_report(
    //         esp_rmaker_device_get_param_by_type(temp_sensor_device, "天气"),
    //         esp_rmaker_str(text));

    // 更新并报告地区参数
    // esp_rmaker_param_update_and_report(
    //         esp_rmaker_device_get_param_by_type(temp_sensor_device, "地区"),
    //         esp_rmaker_str(name));

    // // 更新并报告风力参数
    // esp_rmaker_param_update_and_report(
    //         esp_rmaker_device_get_param_by_type(temp_sensor_device, "风力"),
    //         esp_rmaker_str(wind_class));

    // // 更新并报告湿度参数
    // esp_rmaker_param_update_and_report(
    //         esp_rmaker_device_get_param_by_type(temp_sensor_device, "湿度"),
    //         esp_rmaker_int(rh));

    vTaskDelay(pdMS_TO_TICKS(100));

    // 如果天气文本中包含"雨"字
    if (strstr(text, "雨") != NULL)
    {
        // 触发下雨警告
        esp_rmaker_raise_alert("下雨了，注意带伞!");
    }

    // 如果温度大于20度
    if (temp > 20)
    {
        // 触发高温警告
        esp_rmaker_raise_alert("高温天气，小心中暑!");
    }
    
}

/**
 * @brief HTTP事件处理函数
 *
 * 当HTTP客户端发生事件时，此函数会被调用。
 * 根据事件的类型，执行相应的处理逻辑。
 *
 * @param evt HTTP事件指针
 *
 * @return 返回ESP错误码，成功为ESP_OK
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // 用于存储HTTP请求的响应
    static int output_len;       // 用于存储已读取的字节数
    // 根据事件ID进行分支处理
    switch(evt->event_id) { 
        case HTTP_EVENT_ERROR: // 处理错误事件，打印日志
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED: // 处理连接成功事件，打印日志
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT: // 处理请求头已发送事件，打印日志
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER: // 处理接收到响应头事件，打印日志并显示响应头的键和值。
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA: // 处理接收到响应数据事件
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // 首先检查响应是否使用了分块编码（chunked encoding）
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // 如果不是分块编码，并且user_data有配置，则将响应数据复制到user_data指向的缓冲区。
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else { //如果没有配置user_data，则动态分配一个缓冲区来存储响应数据，并复制数据到该缓冲区。
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH: // 处理请求完成事件
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED: // 处理连接断开事件
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            // 获取并清除最后的TLS错误（如果有）
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        default: break;
    }
    return ESP_OK;
}

/**
 * @brief 使用 URL 执行 HTTP REST 请求
 *
 * 从指定的 URL 中执行 HTTP GET 请求，并解析返回的 JSON 数据以获取天气信息。
 *
 * @note 需要在 ESP-IDF 环境下运行
 */
static void http_rest_with_url(void)
{
    ESP_LOGI(TAG, "HTTP GET from URL");
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    esp_http_client_config_t config = {
        .url = "http://api.map.baidu.com/weather/v1/?district_id=440300&data_type=all&ak=0UpzKOwNGV7iTTkFtRsDmIKWgBxIA8hf",
        .event_handler = _http_event_handler, // 事件处理函数
        .user_data = local_response_buffer, // 用于保存 HTTP 响应数据      
        .disable_auto_redirect = true, // 禁用自动重定向
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                (int)esp_http_client_get_status_code(client), // 获取响应状态码
                (int)esp_http_client_get_content_length(client)); // 获取响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err)); // 将错误码 err 转换为对应的错误字符串，以便更容易理解错误原因
        goto cleanup; // 在失败时直接跳到清理资源部分
    }

    // 处理天气数据
    cJSON *root = cJSON_Parse(local_response_buffer); //根节点
    if (!root) {
        ESP_LOGE(TAG, "No 'result' in JSON");
        goto cleanup_json;
    }
    // cJSON *status = cJSON_GetObjectItem(root,"status"); //节点1

    cJSON *result = cJSON_GetObjectItem(root,"result"); //节点2

    cJSON *location = cJSON_GetObjectItem(result,"location"); //节点2.1
    name = cJSON_GetObjectItem(location,"name")->valuestring;

    cJSON *now = cJSON_GetObjectItem(result,"now"); //节点2.2
    temp = cJSON_GetObjectItem(now,"temp")->valueint;
    text = cJSON_GetObjectItem(now,"text")->valuestring;
    rh = cJSON_GetObjectItem(now,"rh")->valueint;
    wind_class = cJSON_GetObjectItem(now,"wind_class")->valuestring;

    // cJSON *forecasts = cJSON_GetObjectItem(forecasts,"forecasts"); //节点2.3

    // cJSON *message = cJSON *status = cJSON_GetObjectItem(root,"message"); //节点3
    
    ESP_LOGE(TAG, "地区: %s", name);
    ESP_LOGE(TAG, "天气: %s", text);
    ESP_LOGE(TAG, "温度: %d °C", temp);
    ESP_LOGE(TAG, "湿度: %d", rh);
    ESP_LOGE(TAG, "风力: %s", wind_class);

    // 数据上报在JSON数据被释放之前，确保是有效数据
    ESP_LOGI(TAG, "data_report before");
    data_report();
    ESP_LOGI(TAG, "data_report after");

    ESP_LOGI(TAG, "data_ready release before");

    // 在JSON信息解析后释放data_ready信号量, 表示天气信息可以读取
    xSemaphoreGive(data_ready);

    ESP_LOGI(TAG, "data_ready release after");

    // 循环等待json_done信号量收到后，跳出循环，释放资源
    while (xSemaphoreTake(json_done, pdMS_TO_TICKS(10)) != pdTRUE)
    {
       vTaskDelay(pdMS_TO_TICKS(100));
    }

    cleanup_json:
        cJSON_Delete(root);

    cleanup:
        esp_http_client_cleanup(client);

}

/**
 * @brief 将色调转换为 RGB 值
 *
 * 根据给定的色调值，将其转换为 RGB 值。
 *
 * @param hue 色调值，取值范围为 [0, 360]
 */
static void hue_to_rgb(int hue) {
    // 如果色调小于0，则将其设置为0
    if (hue < 0) {
        hue = 0;
    } 

    // 如果色调大于360，则将其设置为360
    if (hue > 360) {
        hue = 360;
    }

    // 如果色调小于120
    if (hue < 120) {
        // 计算红色分量
        base_r = 255 - hue * 2;
        // 计算绿色分量
        base_g = hue * 2;
        // 蓝色分量为0
        base_b = 0;
    // 如果色调在120到240之间
    } else if (hue < 240) {
        // 红色分量为0
        base_r = 0;
        // 计算绿色分量
        base_g = 255 - (hue - 120) * 2;
        // 计算蓝色分量
        base_b = (hue - 120) * 2;
    // 如果色调大于240
    } else {
        // 计算红色分量
        base_r = (hue - 240) * 2;
        // 绿色分量为0
        base_g = 0;
        // 计算蓝色分量
        base_b = 255 - (hue - 240) * 2;
    }
}

/**
 * @brief 写入回调函数
 *
 * 当接收到远程或本地的参数写入请求时，该函数将被调用。
 *
 * @param device ESP RainMaker 设备指针
 * @param param ESP RainMaker 参数指针
 * @param val ESP RainMaker 参数值
 * @param priv_data 私有数据指针
 * @param ctx ESP RainMaker 写入上下文指针
 *
 * @return ESP RainMaker 错误码
 */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    // 如果ctx非空，这意味着回调函数是由远程请求触发的，并通过esp_rmaker_device_cb_src_to_str函数输出请求的来源。
    if (ctx) { 
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }

    // 获取设备名称和参数名称
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);

    // 处理不同的参数:
    ESP_LOGI(TAG, "Received device_name: %s - para_name: %s", device_name, param_name);
    if (strcmp(param_name, "power") == 0) {
        ESP_LOGI(TAG, "Received power = %s ", val.val.b ? "true" : "false");
        if (val.val.b) {
            led_mode = 0; // 打开默认呼吸灯模式
        } else {
            led_mode = 2; // 关闭灯
        }
    } else if (strcmp(param_name, "flow_or_breath") == 0) {
        ESP_LOGI(TAG, "Received flow_or_breath = %s ", val.val.b ? "breath" : "flow");
        if (val.val.b) {
            led_mode = 0; // 呼吸灯模式
        } else {
            led_mode = 1; // 流水灯模式
        }
    } else if (strcmp(param_name, "color") == 0) {
        // 如果参数名为"color"，则记录接收到的整数值，并调用hue_to_rgb函数
        ESP_LOGI(TAG, "Received color = %d ", val.val.i);
        hue_to_rgb(val.val.i);
    } else if (strcmp(param_name, "weather") == 0) {
        ESP_LOGI(TAG, "Received weather = %s ", val.val.b ? "true" : "false");
        if (val.val.b) {
            shouldFetchWeather  = true;
            ESP_LOGI(TAG, "shouldFetchWeather  set to true");
        } 
    } else {
        /* Silently ignoring invalid params */
        return ESP_OK;
    }

    // 使用ESP RainMaker的API更新并报告参数的新值。
    esp_rmaker_param_update_and_report(param, val);

    // 对于其他未处理的参数，函数静默地忽略它们并返回ESP_OK
    return ESP_OK;
}


/**
 * @brief HTTP测试任务
 *
 * 这是一个HTTP测试任务，用于执行HTTP请求。
 *
 * @param pvParameters 任务参数
 */
static void http_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "http_test_task started!");
    while (1) {
        // if (gpio_get_level(GPIO_INPUT_PIN) == 0 || shouldFetchWeather  == true) {     
        if (shouldFetchWeather  == true) { 
            ESP_LOGI(TAG, "Triggering HTTP request");  
            http_rest_with_url();
            shouldFetchWeather  = false;
            ESP_LOGI(TAG, "shouldFetchWeather  set to false");
            // 增加延时，以避免连续触发HTTP请求（如果GPIO仍然为0）
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        } else {
            // 在不满足条件时，使用较短的延时
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}

/**
 * @brief 配置LED灯带 WS2812
 *
 * 根据LED板设计进行LED灯带的一般初始化。
 * 定义了一个名为configure_led的函数，它返回一个led_strip_handle_t类型的值，
 * 这通常是一个句柄或引用，用于后续控制和管理LED灯带。
 *
 * @return LED灯带句柄
 */
led_strip_handle_t configure_led(void)
{
    // 这里定义了一个led_strip_config_t类型的结构体变量strip_config，并为其各个字段赋值
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,   // 连接到LED灯带数据线的GPIO号
        .max_leds = LED_STRIP_LED_NUMBERS,        // LED灯带上的LED数量
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, //  LED灯带的像素格式（这里是GRB）
        .led_model = LED_MODEL_WS2812,            // LED灯带的型号（这里是WS2812）
        .flags.invert_out = false,                // 是否反转输出信号
    };

    // LED灯带后端配置: RMT [RMT是Espressif IoT芯片中的一个硬件特性，通常用于控制各种外设。]
    led_strip_rmt_config_t rmt_config = {
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0) // 在ESP_IDF_VERSION小于5.0.0的情况下，只设置了.rmt_channel字段。
                .rmt_channel = 0,
        #else // 在ESP_IDF_VERSION大于或等于5.0.0的情况下，设置了多个字段，包括：
                .clk_src = RMT_CLK_SRC_DEFAULT,        // RMT的时钟源
                .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT计数器的时钟频率
                .flags.with_dma = false,               // 是否使用DMA（Direct Memory Access）特性。
        #endif
    };

    // 创建LED灯带对象
    led_strip_handle_t led_strip;
    // 使用led_strip_new_rmt_device函数，传入之前配置的strip_config和rmt_config，创建新的LED灯带对象，并将返回的句柄存储在led_strip中
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

/**
 * @brief LED呼吸灯效果
 *
 * 根据给定的LED灯带句柄，实现LED呼吸灯效果。通过循环设置每个LED的颜色，并刷新灯带实现呼吸灯效果。
 *
 * @param led_strip LED灯带句柄
 */
static void led_breth(led_strip_handle_t led_strip) {
    // 当前亮度 = 基础rgb值 * 亮度 /255
    uint8_t color_r = base_r * brightness /255;
    uint8_t color_g = base_g * brightness /255;
    uint8_t color_b = base_b * brightness /255;

    // 循环设置灯的颜色
    for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, color_r, color_g, color_b));
    }

    // 点亮LED: 刷新LED灯带，使设置的颜色生效
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    // 更新亮度值
    brightness += direction;

    // 判断亮度是否达到最大或最小，以改变呼吸灯的方向
    if (brightness == 255 || brightness == 0) {
        direction = -direction;
    }
    
}

/**
 * @brief LED流动效果
 *
 * 在LED灯带上实现流动效果。从第一个LED开始，依次点亮，形成流动的视觉效果。
 *
 * @param led_strip LED灯带句柄
 */
static void led_flow(led_strip_handle_t led_strip) {
    int tail_length = 3;

    // 遍历 LED 灯带上的每个 LED
    for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
        // 清除 LED 灯带上的所有 LED 的颜色
        ESP_ERROR_CHECK(led_strip_clear(led_strip));

        // 遍历尾巴的长度
        for (int j = 0; j < tail_length; j++)
        {
            // 计算当前 LED 的位置
            int position = i - j;
            if (position >= 0) {
                uint8_t tail_r = base_r - (j * (base_r / tail_length));
                uint8_t tail_g = base_g - (j * (base_g / tail_length));
                uint8_t tail_b = base_b - (j * (base_b / tail_length));

                // 设置 LED 灯带上第 j 个 LED 的颜色
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, position, tail_r, tail_g, tail_b));
            }
        }

        // 设置头部
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, base_r, base_g, base_b));

        // 刷新 LED 灯带上的颜色
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        // 延时 100 毫秒
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void led_task(void *pvParameters) {
    led_strip_handle_t led_strip = configure_led();
    while (1)
    {
        if (xSemaphoreTake(data_ready, pdMS_TO_TICKS(10)) == pdTRUE) {
            ESP_LOGI(TAG, "led_task: %s", text);
            if (strstr(text, "晴")) {
                led_mode = 0;
                base_r = 255;
                base_g = 253;
                base_b = 18;
            } else if (strstr(text, "雨")) {
                led_mode = 1;
                base_r = 255;
                base_g = 18;
                base_b = 18;
            } else if (strstr(text, "阴")) {
                led_mode = 1;
                base_r = 160;
                base_g = 32;
                base_b = 240;
            } else if (strstr(text, "云")) {
                led_mode = 1;
                base_r = 240;
                base_g = 230;
                base_b = 140;
            } 
            
            // 在读取完天气内容后，传递信号量给HTTP任务
            xSemaphoreGive(json_done);
        }

        if (led_mode == 0) {
            led_breth(led_strip);
        } else if (led_mode == 1) {
            led_flow(led_strip);
        } else if (led_mode == 2) {
            ESP_ERROR_CHECK(led_strip_clear(led_strip));
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    vTaskDelete(NULL);
}


/**
 * @brief 应用程序主入口函数
 *
 * 初始化NVS存储，连接WiFi，启动RainMaker服务，并创建HTTP测试任务和LED控制任务。
 */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ------------------------------------wifi--------------------------------------start
    // 1. 固定wifi配置信息
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    // ESP_ERROR_CHECK(example_connect());
    // ESP_LOGI(TAG, "Connected to AP, begin http example");
    // 2. 升级wifi配网: 在手机app上通过蓝牙给设备配置wifi连接
    app_wifi_init();
    // ------------------------------------wifi--------------------------------------end

    // ---------------------------------------rainmaker---------------------------------------start
    /**
     * 使用了ESP RainMaker库的ESP32（或其他ESP系列）设备的代码片段，主要用于初始化并配置一个基于RainMaker的物联网应用。
     * ESP RainMaker是Espressif Systems为ESP系列芯片提供的一个云服务，用于远程监控和控制物联网设备。
     * 整个代码片段的主要目的是初始化ESP RainMaker服务，
     * 创建一个物理节点，并在这个节点下添加两个虚拟设备：一个温度传感器和一个LED控制设备。
     * 这些设备都带有一些参数，这些参数可以在远程的ESP RainMaker应用程序上进行查看和控制。
    */

    /** 初始化rainmaker配置参数
            * 定义了一个esp_rmaker_config_t类型的结构体rainmaker_cfg，
            * 并设置enable_time_sync为false，表示不启用时间同步。
     * */ 
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false, 
    };

    //1 物理节点: 使用之前定义的配置初始化一个物理节点，并为其命名为“ESP RainMaker Device”，类型为“Temperature Sensor”。
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Temperature Sensor"); 

    //2 物理节点上: 创建一个温度传感器虚拟设备
    temp_sensor_device = esp_rmaker_device_create("今日天气", "Temperature Sensor", NULL); 
    // 设置温度传感器的回调函数
    esp_rmaker_device_add_cb(temp_sensor_device, write_cb, NULL);

    // esp_rmaker_param_t *temp_param = esp_rmaker_param_create("温度", "温度", esp_rmaker_str(""), PROP_FLAG_READ | PROP_FLAG_WRITE);
    // esp_rmaker_param_t *name_param = esp_rmaker_param_create("地区", "Name", esp_rmaker_str(""), PROP_FLAG_READ | PROP_FLAG_WRITE);
    // esp_rmaker_param_t *text_param = esp_rmaker_param_create("天气", "Name", esp_rmaker_str(""), PROP_FLAG_READ | PROP_FLAG_WRITE);
    // esp_rmaker_param_t *wind_param = esp_rmaker_param_create("风力", "风力", esp_rmaker_str(""), PROP_FLAG_READ | PROP_FLAG_WRITE);
    // esp_rmaker_param_t *rh_param = esp_rmaker_param_create("湿度", "湿度", esp_rmaker_str(""), PROP_FLAG_READ | PROP_FLAG_WRITE);

    // 这些参数随后被添加到温度传感器虚拟设备中。
    // esp_rmaker_device_add_param(temp_sensor_device, temp_param);
    // esp_rmaker_device_add_param(temp_sensor_device, name_param);
    // esp_rmaker_device_add_param(temp_sensor_device, text_param);
    // esp_rmaker_device_add_param(temp_sensor_device, wind_param);
    // esp_rmaker_device_add_param(temp_sensor_device, rh_param);

    // 创建并设置获取天气参数的按钮
    esp_rmaker_param_t *weather_parm = esp_rmaker_param_create("weather", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(weather_parm, ESP_RMAKER_UI_PUSHBUTTON);
    esp_rmaker_device_add_param(temp_sensor_device, weather_parm);

    // 设置主要参数 app上首先显示
    // esp_rmaker_device_assign_primary_param(temp_sensor_device, name_param);

    //3.1 添加虚拟设备-创建的温度传感器虚拟设备添加到物理节点中
    esp_rmaker_node_add_device(node, temp_sensor_device); 

    /** led - 创建一个自定义的设备，用于控制灯的开关和颜色。
        * 下面的代码创建了一个名为“Light”的LED控制设备，并为其添加了一个写回调函数write_cb。
        * 接着，为LED控制设备创建了两个参数：“power”和“color”，并将它们添加到设备中。
     * */ 
    led_control = esp_rmaker_device_create("智能灯带", "Light", NULL);
    // 设置灯带的回调函数
    esp_rmaker_device_add_cb(led_control, write_cb, NULL);
   
    esp_rmaker_param_t *power_param = esp_rmaker_param_create("power", "power", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(power_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(led_control, power_param);

    esp_rmaker_param_t *flow_or_breath_param = esp_rmaker_param_create("flow_or_breath", "toggle", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(flow_or_breath_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(led_control, flow_or_breath_param);
    
    esp_rmaker_param_t *color_param = esp_rmaker_param_create("color", "color", esp_rmaker_int(0), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(color_param, ESP_RMAKER_UI_HUE_SLIDER);
    esp_rmaker_param_add_bounds(color_param, esp_rmaker_int(0), esp_rmaker_int(360), esp_rmaker_int(1));
    esp_rmaker_device_add_param(led_control, color_param);

    // 设置主要参数 app上首先显示
    esp_rmaker_device_assign_primary_param(led_control, power_param);

    //3.2 添加虚拟设备-温度传感器到物理节点中
    esp_rmaker_node_add_device(node, led_control); 

    // 4 启动rainmaker
    esp_rmaker_start(); 

    // 启动WiFi连接，并使用随机生成的密钥: POP_TYPE_RANDOM 自动生成随机密钥
    app_wifi_start(POP_TYPE_RANDOM);
    // ---------------------------------------rainmaker---------------------------------------end

    data_ready = xSemaphoreCreateBinary();
    json_done = xSemaphoreCreateBinary();

    // 【HTTP任务】
    xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);

    // 流程：解析JSON数据【HTTP任务】 -> 使用被解析后的数据【LED任务】 -> 释放JSON数据【HTTP任务】
    // 1， LED 任务只使用被完全解析后的天气信息
    // 2，LED 任务要在JSON数据被释放之前完成读取
    xTaskCreate(&led_task, "led_task", 8192, NULL, 5, NULL);
}
