#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mdns.h"
#include "mbedtls/base64.h"

// Configuration
#define SSID "FutureLab2103"
#define PASSWORD "CableWalker2020"

#define LED_GPIO 2
#define SERVO_GPIO 18
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_HIGH_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_RESOLUTION LEDC_TIMER_13_BIT

#define USERNAME "sezam"
#define PASSWORDAUTH "open"

static const char *TAG = "Servo Control";
static httpd_handle_t server = NULL;

// HTML page (same as original)
static const char *HTML =
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Door Control</title>"
    "<style>"
        "body { font-family: Arial; text-align: center; margin-top: 50px; }"
        "button { padding: 20px; font-size: 20px; margin: 10px; cursor: pointer; }"
        ".open-btn { background-color: #4CAF50; color: white; }"
        ".close-btn { background-color: #f44336; color: white; }"
        ".auto-btn { background-color: #2196F3; color: white; }"
    "</style>"
"</head>"
"<body>"
    "<h1>ESP32 Door Control</h1>"
    "<p>Status: <strong id=\"status\">Unknown</strong></p>"

    "<form method='POST' action='/open'>"
        "<button type='submit' class='open-btn'>Open Door</button>"
    "</form>"

    "<form method='POST' action='/close'>"
        "<button type='submit' class='close-btn'>Close Door</button>"
    "</form>"

    "<form method='POST' action='/auto'>"
        "<button type='submit' class='auto-btn'>Open & Auto Close</button>"
    "</form>"

    "<script>"
        "function updateStatus() {"
            "fetch('/status')"
                ".then(res => res.json())"
                ".then(data => {"
                    "document.getElementById('status').innerText = data.status;"
                "});"
        "}"
        "setInterval(updateStatus, 2000);"
        "updateStatus();"
    "</script>"
"</body>"
"</html>";

bool authenticate(httpd_req_t *req) {
    char auth_buf[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) == ESP_OK) {
        if (strncmp(auth_buf, "Basic ", 6) != 0) return false;

        char decoded[64];
        size_t decoded_len;

        int ret = mbedtls_base64_decode((unsigned char *)decoded, sizeof(decoded), &decoded_len,
                                         (const unsigned char *)(auth_buf + 6), strlen(auth_buf) - 6);
        if (ret == 0) {
            decoded[decoded_len] = '\0';
            if (strstr(decoded, USERNAME) && strstr(decoded, PASSWORDAUTH)) {
                return true;
            }
        }
    }
    return false;
}

static bool door_open = false;

void set_servo_angle(int angle) {
    const int min_us = 500;
    const int max_us = 2500;
    uint32_t duty = ((min_us + angle * (max_us - min_us) / 180) * (1 << LEDC_RESOLUTION)) / 20000;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    if (angle == 90) {
        gpio_set_level(LED_GPIO, 1);
        door_open = true;
    } else {
        gpio_set_level(LED_GPIO, 0);
        door_open = false;
    }
}

// HTTP Handlers
esp_err_t status_handler(httpd_req_t *req) {
    if (!authenticate(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const char *status = door_open ? "Open" : "Closed";

    char response[64];
    snprintf(response, sizeof(response), "{\"status\":\"%s\"}", status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t root_handler(httpd_req_t *req) {
    if (!authenticate(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML, strlen(HTML));
    return ESP_OK;
}

esp_err_t open_handler(httpd_req_t *req) {
    if (!authenticate(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    set_servo_angle(90);
    gpio_set_level(LED_GPIO, 1);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t close_handler(httpd_req_t *req) {
    if (!authenticate(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Closing door (0°)");
    set_servo_angle(0);
    gpio_set_level(LED_GPIO, 0);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t auto_handler(httpd_req_t *req) {
    if (!authenticate(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Открываем дверь
    set_servo_angle(90);
    gpio_set_level(LED_GPIO, 1);

    // Ждём 2 секунды
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Закрываем дверь
    set_servo_angle(0);
    gpio_set_level(LED_GPIO, 0);

    // Перенаправляем обратно
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_angle_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Received /setAngle request");

    char buffer[100];
    if (httpd_req_get_url_query_str(req, buffer, sizeof(buffer)) == ESP_OK) {
        ESP_LOGI(TAG, "Query: %s", buffer);

        char angle_str[10];
        if (httpd_query_key_value(buffer, "angle", angle_str, sizeof(angle_str)) == ESP_OK) {
            int angle = atoi(angle_str);
            ESP_LOGI(TAG, "Parsed angle: %d", angle);
            set_servo_angle(angle);
        } else {
            ESP_LOGE(TAG, "Failed to parse 'angle' parameter");
        }
    } else {
        ESP_LOGE(TAG, "Failed to get query string");
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// WiFi Initialization
void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// Web Server Setup
void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    httpd_uri_t angle_uri = {
        .uri = "/setAngle",
        .method = HTTP_GET,
        .handler = set_angle_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &angle_uri);
    }
}

// Servo Initialization
void servo_init() {
    // Настройка GPIO для LED
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(LED_GPIO, 0); // Сначала выключен

    // Настройка таймера и канала для серво
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0
    };
    ledc_channel_config(&ch_cfg);
}

void initialize_mdns() {
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "MDNS Init failed: %d", err);
        return;
    }

    mdns_hostname_set("esp32-door");  // Set hostname
    mdns_instance_name_set("ESP32 Door Controller");

    // Advertise HTTP service
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    servo_init();
    wifi_init();

    // Wait for WiFi connection
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize mDNS
    initialize_mdns();

    // Print IP address to serial
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    ESP_LOGI(TAG, "Access via: http://esp32-door.local");

    start_webserver();
    httpd_uri_t open_uri = {
        .uri = "/open",
        .method = HTTP_POST,
        .handler = open_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &open_uri);

    httpd_uri_t close_uri = {
        .uri = "/close",
        .method = HTTP_POST,
        .handler = close_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &close_uri);
    httpd_uri_t auto_uri = {
        .uri = "/auto",
        .method = HTTP_POST,
        .handler = auto_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &auto_uri);
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
}
