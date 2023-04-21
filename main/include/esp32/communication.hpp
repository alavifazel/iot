#ifndef ESP32COM_H
#define ESP32COM_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"
#define BLINK_GPIO CONFIG_BLINK_GPIO
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string>
#include "esp_netif.h"
#include <sys/param.h>
#include "esp_system.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>
#include <time.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "freertos/queue.h"
#include "driver/gpio.h"


#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4
#define KEEPALIVE_IDLE 4000
#define KEEPALIVE_INTERVAL 4000
#define KEEPALIVE_COUNT 4000
#define PORT 4000
#define CONFIG_EXAMPLE_IPV4 1

//____________________________________
//__________GPIO_CONFIG_______________
#define GPIO_OUTPUT_IO_0    0
#define GPIO_OUTPUT_IO_1    0   
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))
#define GPIO_INPUT_IO_0     1
#define GPIO_INPUT_IO_1     1
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1))
#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;

//_____________________________________
//_______________END___________________


namespace cadmium::iot
{

    enum Phase
    {
        WAITING_FOR_DATA,
        DATA_RECEIVED
    };

    template <class T>
    class ESP32COM
    {
        //____________________________________
        //_____________TCPServerState_________
        class TCPServerState
        {
        private:
            int addrFamily;
            bool dataIsAvailable;
            std::string data;
            double sigma;
            int port;
            T *connection;

        public:
            TCPServerState(int addrFamily, T *connection, int port)
                : addrFamily{addrFamily}, dataIsAvailable{false}, data{}, sigma{1}, port(port), connection{connection} {}
            TCPServerState() {}
            int getAddrFamily() const { return this->addrFamily; }
            bool dataAvailable() const
            {
                return this->dataIsAvailable;
            }
            void setDataAvailability(bool b) { this->dataIsAvailable = b; }
            void setData(char data[]) { this->data = data; }
            std::string getData() const
            {
                // dataAvailable = false;
                return this->data;
            }
            double getSigma() const { return this->sigma; }
            void setSigma(double s) { this->sigma = sigma; }
            T *getConnection() { return connection; }
            int getPort() { return this->port; }
            void setPort(int port) { this->port = port; }
        };

        //___________________________________
        //______________END__________________
    public:
        TCPServerState tcpServerState;

        ESP32COM(const char *tag)
            : tag{tag}
        {
        }

        void setupConnection(T *obj, const char *tag, const char *ssid, const char *pwd, int tcpPort)
        {
            WifiAPSetup(ssid, pwd);
            TCPServerSetup(tcpPort, obj);
        }

        void log(const char *msg) const
        {
            ESP_LOGI(tag, "%s", msg);
        }

        void log(std::string msg) const
        {
            ESP_LOGI(tag, "%s", msg.c_str());
        }

        void WifiAPSetup(const char *ssid, const char *pwd, int channel = 1, int maxSTAConn = 4)
        {
            esp_err_t ret = nvs_flash_init();
            if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
            {
                ESP_ERROR_CHECK(nvs_flash_erase());
                ret = nvs_flash_init();
            }

            ESP_ERROR_CHECK(esp_netif_init());
            ESP_ERROR_CHECK(esp_event_loop_create_default());
            esp_netif_create_default_wifi_ap();

            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));

            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                                ESP_EVENT_ANY_ID,
                                                                &wifi_event_handler,
                                                                NULL,
                                                                NULL));

            wifi_config_t wifi_config = {};
            strcpy((char *)wifi_config.sta.ssid, ssid);
            strcpy((char *)wifi_config.sta.password, pwd);
            // wifi_config.ap.ssid_len = strlen(ssid);
            // wifi_config.ap.channel = channel;
            wifi_config.ap.max_connection = maxSTAConn;
            wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

            if (strlen(pwd) == 0)
            {
                wifi_config.ap.authmode = WIFI_AUTH_OPEN;
            }

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
        }

        void TCPServerSetup(int port, T *obj)
        {
            tcpServerState = TCPServerState(AF_INET, obj, port);
            xTaskCreate(tcp_server_task, "tcp_server", tcpServerState.getPort(), (void *)&tcpServerState, 5, NULL);
        }

        static double getTime()
        {
            time_t timer;
            struct tm y2k = {0};

            y2k.tm_hour = 0;
            y2k.tm_min = 0;
            y2k.tm_sec = 0;
            y2k.tm_year = 100;
            y2k.tm_mon = 0;
            y2k.tm_mday = 1;
            time(&timer);
            return difftime(timer, mktime(&y2k));
        }

        static inline double timeReference;

        //____________GPIO______________
        //______________________________

        static void IRAM_ATTR gpio_isr_handler(void* arg)
        {
            uint32_t gpio_num = (uint32_t) arg;
            xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
        }

        static void gpio_task_example(void* arg)
        {
            uint32_t io_num;
            for(;;) {
                if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
                    printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
                }
            }
        }
        //______________________________
        //_____________END______________

    protected:
        const char *tag;

        // Used by the station setup function
        static EventGroupHandle_t s_wifi_event_group;

        static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data)
        {
            if (event_id == WIFI_EVENT_AP_STACONNECTED)
            {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            }
            else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
            {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            }
        }
        static void do_retransmit(const int sock, TCPServerState *state)
        {
            int len;
            char rx_buffer[128];

            do
            {
                len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len < 0)
                {
                    ESP_LOGE("TAG", "Error occurred during receiving: errno %d", errno);
                }
                else if (len == 0)
                {
                    // ESP_LOGW("TAG", "Connection closed");
                }
                else
                {
                    rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
                    // ESP_LOGI("TAG", "Received %d bytes: %s", len, rx_buffer);
                    state->setData(rx_buffer);
                    state->getConnection()->getState().setDataAvailable(true);

                    // send() can return less bytes than supplied length.
                    // Walk-around for robust implementation.
                    int to_write = len;
                    while (to_write > 0)
                    {
                        int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                        if (written < 0)
                        {
                            ESP_LOGE("TAG", "Error occurred during sending: errno %d", errno);
                            // Failed to retransmit, giving up
                            return;
                        }
                        to_write -= written;
                    }
                }
            } while (len > 0);

            timeReference = getTime();
        }

        static void tcp_server_task(void *pvParameters)
        {
            TCPServerState *state = (TCPServerState *)pvParameters;
            char addr_str[128];
            int addr_family = state->getAddrFamily();
            int ip_protocol = 0;
            int keepAlive = 1;
            int keepIdle = KEEPALIVE_IDLE;
            int keepInterval = KEEPALIVE_INTERVAL;
            int keepCount = KEEPALIVE_COUNT;
            struct sockaddr_storage dest_addr;

#ifdef CONFIG_EXAMPLE_IPV4
            if (addr_family == AF_INET)
            {
                struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
                dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
                dest_addr_ip4->sin_family = AF_INET;
                dest_addr_ip4->sin_port = htons(PORT);
                ip_protocol = IPPROTO_IP;
            }
#endif
#ifdef CONFIG_EXAMPLE_IPV6
            if (addr_family == AF_INET6)
            {
                struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
                bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
                dest_addr_ip6->sin6_family = AF_INET6;
                dest_addr_ip6->sin6_port = htons(PORT);
                ip_protocol = IPPROTO_IPV6;
            }
#endif

            int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
            if (listen_sock < 0)
            {
                ESP_LOGE("TAG", "Unable to create socket: errno %d", errno);
                vTaskDelete(NULL);
                return;
            }
            int opt = 1;
            setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

            // ESP_LOGI(TAG, "Socket created");

            int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0)
            {
                ESP_LOGE("TAG", "Socket unable to bind: errno %d", errno);
                ESP_LOGE("TAG", "IPPROTO: %d", addr_family);
                goto CLEAN_UP;
            }
            // ESP_LOGI("TAG", "Socket bound, port %d", PORT);

            err = listen(listen_sock, 1);
            if (err != 0)
            {
                ESP_LOGE("TAG", "Error occurred during listen: errno %d", errno);
                goto CLEAN_UP;
            }

            while (1)
            {
                timeReference = getTime();
                // ESP_LOGI("TAG", "Socket listening");

                struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
                socklen_t addr_len = sizeof(source_addr);
                int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
                if (sock < 0)
                {
                    ESP_LOGE("TAG", "Unable to accept connection: errno %d", errno);
                    break;
                }

                // Set tcp keepalive option
                setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
// Convert ip address to string
#ifdef CONFIG_EXAMPLE_IPV4
                if (source_addr.ss_family == PF_INET)
                {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                }
#endif
#ifdef CONFIG_EXAMPLE_IPV6
                if (source_addr.ss_family == PF_INET6)
                {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
#endif
                // ESP_LOGI("TAG", "Socket accepted ip address: %s", addr_str);
                do_retransmit(sock, state);

                shutdown(sock, 0);
                close(sock);
            }

        CLEAN_UP:
            close(listen_sock);
            vTaskDelete(NULL);
        }
    };

    void printChipInfo()
    {
        /* Print chip information */
        esp_chip_info_t chip_info;
        uint32_t flash_size;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU core(s), WiFi%s%s%s, ",
               CONFIG_IDF_TARGET,
               chip_info.cores,
               (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
               (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
               (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

        unsigned major_rev = chip_info.revision / 100;
        unsigned minor_rev = chip_info.revision % 100;
        printf("silicon revision v%d.%d, ", major_rev, minor_rev);
        if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
        {
            printf("Get flash size failed");
            return;
        }

        printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

        printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    }

    void gpioOut(int gpio, int delay, uint32_t level) {
        vTaskDelay(delay / portTICK_PERIOD_MS);
        gpio_set_level((gpio_num_t)GPIO_OUTPUT_IO_0, level);
        gpio_set_level((gpio_num_t)GPIO_OUTPUT_IO_1, level);
    }
}

#endif