/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h" // missing (for MACSTR)
#include <esp_http_server.h> // WebServer
#include "esp_netif.h" // For setting static IP
#include "freertos/event_groups.h" // Event groups

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"

#define TINYPICO_ESP_WIFI_SSID      "TinyPICO-Soft-AP"
#define TINYPICO_ESP_WIFI_PASS      "1234567890"
#define TINYPICO_ESP_WIFI_CHANNEL   1
#define TINYPICO_MAX_STA_CONN       4
#define TINYPICO_HTTP_QUERY_KEY_MAX_LEN  (64)
#define TINYPICO_MAXIMUM_RETRY         5
#define TINYPICO_STATIC_IP_ADDR        "192.168.0.1"
#define TINYPICO_STATIC_NETMASK_ADDR   "255.255.255.0"
#define TINYPICO_STATIC_GW_ADDR        "192.168.0.1"
#ifdef CONFIG_TINYPICO_STATIC_DNS_AUTO
#define TINYPICO_MAIN_DNS_SERVER       TINYPICO_STATIC_GW_ADDR
#define TINYPICO_BACKUP_DNS_SERVER     "0.0.0.0"
#else
#define TINYPICO_MAIN_DNS_SERVER       CONFIG_TINYPICO_STATIC_DNS_SERVER_MAIN
#define TINYPICO_BACKUP_DNS_SERVER     CONFIG_TINYPICO_STATIC_DNS_SERVER_BACKUP
#endif
#ifdef CONFIG_TINYPICO_STATIC_DNS_RESOLVE_TEST
#define TINYPICO_RESOLVE_DOMAIN        CONFIG_TINYPICO_STATIC_RESOLVE_DOMAIN
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_DATA_SIZE 512

/*******************************
 * WiFi Config
 *******************************/
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static const char *TAG = "TinyPICO softAP StaticIP";

static esp_err_t set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{
    if (addr && (addr != IPADDR_NONE)) {
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = addr;
        dns.ip.type = IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
    }
    return ESP_OK;
}

static void set_static_ip(esp_netif_t *netif)
{
    if (esp_netif_dhcpc_stop(netif) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop dhcp client");
        return;
    }
    if (esp_netif_dhcps_stop(netif) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop dhcp server");
        return;
    }
    esp_netif_ip_info_t ip;
    memset(&ip, 0 , sizeof(esp_netif_ip_info_t));
    ip.ip.addr = ipaddr_addr(TINYPICO_STATIC_IP_ADDR);
    ip.netmask.addr = ipaddr_addr(TINYPICO_STATIC_NETMASK_ADDR);
    ip.gw.addr = ipaddr_addr(TINYPICO_STATIC_GW_ADDR);
    esp_err_t ret = esp_netif_set_ip_info(netif, &ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ip info.  ErrCode: 0x%x", ret);
        return;
    }
    ESP_LOGD(TAG, "Success to set static ip: %s, netmask: %s, gw: %s", TINYPICO_STATIC_IP_ADDR, TINYPICO_STATIC_NETMASK_ADDR, TINYPICO_STATIC_GW_ADDR);
    ESP_ERROR_CHECK(set_dns_server(netif, ipaddr_addr(TINYPICO_MAIN_DNS_SERVER), ESP_NETIF_DNS_MAIN));
    ESP_ERROR_CHECK(set_dns_server(netif, ipaddr_addr(TINYPICO_BACKUP_DNS_SERVER), ESP_NETIF_DNS_BACKUP));
    esp_netif_dhcps_start(netif);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap()
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_ap();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = TINYPICO_ESP_WIFI_SSID,
            .ssid_len = strlen(TINYPICO_ESP_WIFI_SSID),
            .channel = TINYPICO_ESP_WIFI_CHANNEL,
            .password = TINYPICO_ESP_WIFI_PASS,
            .max_connection = TINYPICO_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(TINYPICO_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Static IP
    ESP_LOGI(TAG, "TinyPICO_Setting_IP");
    set_static_ip(sta_netif);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             TINYPICO_ESP_WIFI_SSID, TINYPICO_ESP_WIFI_PASS, TINYPICO_ESP_WIFI_CHANNEL);
}

/*******************************
 * Web Server
 *******************************/
static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[MAX_DATA_SIZE];
    
    // Read the data received in the POST request
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                // Handle timeout
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;

        // Print the received data
        ESP_LOGI(TAG, "Received data: %.*s", ret, buf);
    }

    // Send a response to the client
    const char resp_str[] = "Data received successfully";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t save_uri = {
    .uri       = "/save",
    .method    = HTTP_POST,
    .handler   = save_handler,
    .user_ctx  = NULL
};

// HTTP GET handler to handle requests for the root endpoint
esp_err_t index_handler(httpd_req_t *req) {
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = index_html_end - index_html_start;

    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &save_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

/*******************************
 * Application
 *******************************/
void app_main()
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // WiFi
    ESP_LOGI(TAG, "TinyPICO_WiFi_Mode_AP");
    wifi_init_softap();

    // Web Server
    static httpd_handle_t server = NULL;
    server = start_webserver();
    while (server) {
        sleep(5);
    }
}

/* References:
 * https://esp32tutorials.com/esp32-access-point-ap-esp-idf/ 
 * https://www.esp32.com/viewtopic.php?t=26456
 * ChatGPT
 */