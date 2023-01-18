
//Cogemos de base el proyecto de ejemplo examples/protocols/mqtt/tcp

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_http_client.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "max30100.h"

#include "ssd1306.h"
#include "font8x8_basic.h"

#define tag "SSD1306"
#define I2C_SDA 26
#define I2C_SCL 25
#define I2C_FRQ 100000
#define I2C_PORT I2C_NUM_0

SSD1306_t dev;

max30100_config_t max30100 = {};

static const char *TAG = "MQTT_EXAMPLE";


/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

/* TAGs for the system*/
static const char *TAG1 = "HTTP_CLIENT Handler";
static const char *TAG2 = "wifi station";
static const char *TAG3 = "Sending getUpdates";
static const char *TAG4 = "Sending sendMessage";

/*WIFI configuration*/
#define ESP_WIFI_SSID      "SBC"
#define ESP_WIFI_PASS      "SBCwifi$"
#define ESP_MAXIMUM_RETRY  10

/*Telegram configuration*/
#define TOKEN "5926270059:AAFK2nqVU9c2hkJwiHUv248Ws5xEtlFDfuI"
char url_string[512] = "https://api.telegram.org/bot"; // /sendMessage
// Using in the task strcat(url_string,TOKEN)); the main direct from the url will be in url_string
//The chat id that will receive the message
#define chat_ID1 "@SBC22M06"
#define chat_ID2 "-1001711852851" 

//#define chat_ID1 "@SBCM06"
//#define chat_ID2 "346494192" 

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

double BPM = 116;
double SPO2 = 92.95;

char updateId[8] = "";
char comando[150] = "";
char text[7] = "";
char vacio[150] = "";

char estado[150] = "/estado";
char datos[150] = "/datos";



esp_err_t i2c_master_init(i2c_port_t i2c_port){
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.scl_io_num = I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FRQ;
    ESP_ERROR_CHECK(i2c_param_config(i2c_port, &conf));
    return i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
}

void delay(int number_of_seconds)
{
    // Converting time into milli_seconds
    int milli_seconds = 1000 * number_of_seconds;
 
    // Storing start time
    clock_t start_time = clock();
 
    // looping till required time is not achieved
    while (clock() < start_time + milli_seconds);
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
 esp_mqtt_client_handle_t client = event->client;
 int msg_id;
 switch (event->event_id) {
 case MQTT_EVENT_CONNECTED:
 ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
 break;
 case MQTT_EVENT_DISCONNECTED:
 ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
 break;
 case MQTT_EVENT_SUBSCRIBED:
 ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");
 break;
 case MQTT_EVENT_UNSUBSCRIBED:
 ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED");
 break;
 case MQTT_EVENT_PUBLISHED:
 ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
 break;
 case MQTT_EVENT_DATA:
 ESP_LOGI(TAG, "MQTT_EVENT_DATA");
 printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
 printf("DATA=%.*s\r\n", event->data_len, event->data);
 break;
 case MQTT_EVENT_ERROR:
 ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
 break;
 default:
 ESP_LOGI(TAG, "Other event id:%d", event->event_id);
 break;
 }
 return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
void *event_data) {

 ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
 mqtt_event_handler_cb(event_data);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG2, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG2,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG2, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG2, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG2, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG2, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG2, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG1, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG1, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG1, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG1, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG1, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG1, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG1, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static void https_telegram_getUpdates_perform(void) {
	char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    char buffer_AUX[MAX_HTTP_OUTPUT_BUFFER] = {0};
	char url[512] = "";

    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = buffer,        // Pass address of local buffer to get response
    };
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Adding the method
    strcat(url,"/getUpdates?limit=1&offset=-1");
    //strcat(url, id_comando);
    //ESP_LOGW(TAG3, "el id_comando es: %d",id_comando);
    ESP_LOGW(TAG3, "url es: %s",url);
    //ESP_LOGW(TA1, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //You set the real url for the request
    esp_http_client_set_url(client, url);
    //ESP_LOGW(TAG1, "Selecting the http method");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    //ESP_LOGW(TAG1, "Perform");
    esp_err_t err = esp_http_client_perform(client);

    //ESP_LOGW(TAG, "Revisare");
    if (err == ESP_OK) {
        ESP_LOGI(TAG3, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        ESP_LOGW(TAG3, "Desde Perform el output es: %s",buffer);

        char * foundI = strstr(buffer, "text"); //Encuentra la palabra text dentro de los datos del buffer
        char * foundF = strstr(buffer, "entities"); //Encuentra la palabra entities dentro de los datos del buffer

        char * found1I = strstr(buffer, "update_id"); //Encuentra la palabra text dentro de los datos del buffer
        char * found1F = strstr(buffer, "\"message\""); //Encuentra la palabra entities dentro de los datos del buffer

        //char updateId[8] = "";
        //char text[100] = "";
        if( foundI != NULL) 
        {
            printf("found1F: %s \n", found1F);
            int index = foundI - &buffer[0] + 7;
            int index2 = foundF - &buffer[0] - 3;

            int index3 = found1I - &buffer[0] + 11;
            int index4 = found1F - &buffer[0] - 2;

            printf("El buffer tiene un comando \n");
            printf("index: %d \n", index);
            index2 = index2 - index;
            printf("Antes\n");
            index4 = index4 - index3;
            printf("Despues \n");
            strncpy(updateId, &buffer[index3], index4);
            printf("index4: %d \n", index4);
            printf("UpdateIdAntes: %s \n", updateId);
            strncpy(text, &buffer[index], index2);
            //strncpy(comando, &buffer[index], index2);
            printf("UpdateIdDespues: %s \n", updateId);

            //printf("Text: %s \n", text);
            //int ret =  strcmp(text, "/estado");
            //printf("Iguales: %d \n", ret);
           
        } else{
            printf("El buffer NO tiene comandos \n");
        }
    } else {
        ESP_LOGE(TAG3, "Error perform http request %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG3, "Cerrar Cliente");
    esp_http_client_close(client);
    ESP_LOGW(TAG1, "Limpiare");
    esp_http_client_cleanup(client);
}


static void https_telegram_sendMessage_perform_post(void) {


	/* Format for sending messages
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=[CHANNEL_NAME]&text=[MESSAGE_TEXT]

	For public groups you can use
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=@GroupName&text=hello%20world
	For private groups you have to use the chat id (which also works with public groups)
	https://api.telegram.org/bot[BOT_TOKEN]/sendMessage?chat_id=-1234567890123&text=hello%20world

	You can add your chat_id or group name, your api key and use your browser to send those messages
	The %20 is the hexa for the space

	The format for the json is: {"chat_id":852596694,"text":"Message using post"}
	*/

	char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
		.user_data = output_buffer,
    };
    //POST
    ESP_LOGW(TAG4, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Passing the method
    strcat(url,"/sendMessage");
    //ESP_LOGW(TAG4, "url string es: %s",url);
    //You set the real url for the request
    esp_http_client_set_url(client, url);


	ESP_LOGW(TAG4, "Enviare POST");
	/*Here you add the text and the chat id
	 * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
	  */
	// The example had this, but to add the chat id easierly I decided not to use a pointer
	//const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
	char post_data[512] = "";

    int igualdad = strcmp(text, datos);
    int igualdad2 = strcmp(text, estado);

    if(igualdad == 0 || igualdad == 111)
    {
        printf("Comando: %s", text);
        sprintf(post_data,"{\"chat_id\":%s,\"text\":\"BPM: %3.f || SPO2: %2.3f\"}",chat_ID2, BPM, SPO2);
    }else if(igualdad2 == 0)
    {   
        if (BPM > 115)
        {
            sprintf(post_data,"{\"chat_id\":%s,\"text\":\"Ritmo cardiaco demasiado alto, puede ser sintoma de taquicardia.\"}",chat_ID2);
        }else if(BPM < 60){
            sprintf(post_data,"{\"chat_id\":%s,\"text\":\"Ritmo cardiaco demasiado bajo, puede ser sintoma de bradicardia.\"}",chat_ID2);
        }else{
            printf("Comando: %s", text);
            sprintf(post_data,"{\"chat_id\":%s,\"text\":\"Ritmos cardiaco normal.Tienes buena salud.\"}",chat_ID2);
        }
    }else{
        sprintf(post_data,"{\"chat_id\":%s,\"text\":\"Error de comando, ese comando no existe.\"}",chat_ID2);
    }
    
    printf("Igual: %d", igualdad);
    //sprintf(post_data,"{\"chat_id\":%s,\"text\":\"BPM: %3.f\"}",chat_ID2, BPM);

    //ESP_LOGW(TAG, "El json es es: %s",post_data);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG4, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        ESP_LOGW(TAG4, "Desde Perform el output es: %s",output_buffer);

    } else {
        ESP_LOGE(TAG4, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG1, "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG4, "esp_get_free_heap_size: %d", esp_get_free_heap_size ());
}


static void http_test_task(void *pvParameters) {
    /* Creating the string of the url*/
    // You concatenate the host with the Token so you only have to write the method
    
    strcat(url_string,TOKEN);
    ESP_LOGW(TAG1, "Wait 2 second before start");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
	
    char updateId_aux[8] = "";

    while (true)
    {      
        ESP_LOGW(TAG1, "https_telegram_getUpdates_perform");
        https_telegram_getUpdates_perform();
        ESP_LOGW(TAG1, "https_telegram_sendMessage_perform_post");
        printf("Primer caracter: %c \n", comando[0]);
        if (strcmp(updateId, updateId_aux) != 0)
        {
            printf("UpdateId_Aux: %s \n", updateId_aux);
            printf("UpdateId: %s \n", updateId);
            
            https_telegram_sendMessage_perform_post();
            strncpy(updateId_aux, &updateId[0],sizeof(updateId));
            //memset(updateId_aux, updateId, sizeof(updateId));
            
            printf("Caracter NULL: %c \n", comando[0]);
        }
    }

    ESP_LOGI(TAG1, "Finish http example");
    vTaskDelete(NULL);
}

void get_bpm(void* param) {

    esp_adc_cal_characteristics_t adc1_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);

    uint32_t voltageX;
    uint32_t voltageY;
    uint32_t voltageZ;

    dev._address = I2CAddress;
	dev._flip = false;

    #if CONFIG_SSD1306_128x64
	ssd1306_init(&dev, 128, 64);
    #endif // CONFIG_SSD1306_128x64
    ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);

    #if CONFIG_SSD1306_128x64
    ssd1306_display_text(&dev, 1, "    BPM:", 8, false);
	
	ssd1306_display_text(&dev, 5, "    Sp02:", 9, false);
    #endif // CONFIG_SSD1306_128x64

    max30100_data_t result = {};
     esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://demo.thingsboard.io",
        .event_handle = mqtt_event_handler,
        .port = 1883,
        //Ponemos el token del device ESP32_SENSOR que nos sale en Thingsboard
        .username = "CRCsXXLY5wbsKc2MpmcX", 
 };
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
  esp_mqtt_client_start(client);
    while(true) {
    /*
        int X_raw = adc1_get_raw(ADC1_CHANNEL_5); //PIN 33
        int Y_raw = adc1_get_raw(ADC1_CHANNEL_7); // PIN 35
        int Z_raw = adc1_get_raw(ADC1_CHANNEL_3); //PIN 39

        ESP_LOGI(TAG,"X_raw = %d", X_raw);
        ESP_LOGI(TAG,"Y_raw = %d", Y_raw);
        ESP_LOGI(TAG,"Z_raw = %d", Z_raw);

        float X_true = (((float)X_raw - 331.5)/65*9.8);
        float Y_true = (((float)Y_raw - 329.5)/68.5*9.8);
        float Z_true = (((float)Z_raw - 340.5)/68*9.8);

        ESP_LOGI(TAG,"X = %f", X_true);
        ESP_LOGI(TAG,"Y = %f", Y_true);
        ESP_LOGI(TAG,"Z = %f", Z_true);

        voltageX = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_5), &adc1_chars);
        ESP_LOGI(TAG, "voltageX: %d mV", voltageX);
        voltageY = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
        ESP_LOGI(TAG, "voltageY: %d mV", voltageY);
        voltageZ = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_3), &adc1_chars);
        ESP_LOGI(TAG, "voltageZ: %d mV", voltageZ);
        */
        //Update sensor, saving to "result"
        ESP_ERROR_CHECK(max30100_update(&max30100, &result));
        if(result.pulse_detected) {

            printf("BPM: %f | SpO2: %f%%\n", result.heart_bpm, result.spO2);
            BPM = (double)result.heart_bpm;
            SPO2 = (double)result.spO2+3;
        
        #if CONFIG_SSD1306_128x64
            char txtBPM[20];
            char txtSp02[20];

        sprintf(txtBPM, "    %.1f" , BPM);
        sprintf(txtSp02, "    %.1lf%%" , SPO2);

        int tamBPM = strlen(txtBPM);
        int tamSp02 = strlen(txtSp02);


        ssd1306_display_text(&dev, 2, txtBPM, tamBPM, false);

        ssd1306_display_text(&dev, 6, txtSp02, tamSp02, false);
        #endif // CONFIG_SSD1306_128x64   

        cJSON *root = cJSON_CreateObject();

        cJSON_AddNumberToObject(root, "BPM", BPM);
        cJSON_AddNumberToObject(root, "SpO2", SPO2);
        char *post_data = cJSON_PrintUnformatted(root);
        esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0);
        cJSON_Delete(root);
        free(post_data);

            
        }
        
        //Update rate: 100Hz
        //vTaskDelay(10/portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    
    //Init I2C_NUM_0
    ESP_ERROR_CHECK(i2c_master_init(I2C_PORT));
    //Init sensor at I2C_NUM_0
    ESP_ERROR_CHECK(max30100_init( &max30100, I2C_PORT,
                   MAX30100_DEFAULT_OPERATING_MODE,
                   MAX30100_DEFAULT_SAMPLING_RATE,
                   MAX30100_DEFAULT_LED_PULSE_WIDTH,
                   MAX30100_DEFAULT_IR_LED_CURRENT,
                   MAX30100_DEFAULT_START_RED_LED_CURRENT,
                   MAX30100_DEFAULT_MEAN_FILTER_SIZE,
                   MAX30100_DEFAULT_PULSE_BPM_SAMPLE_SIZE,
                   true, false ));

    
 /* 
 ESP_LOGI(TAG, "[APP] Startup..");
 ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
 ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
 esp_log_level_set("*", ESP_LOG_INFO);
 esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
 esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
 esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
 esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
 esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
 esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);
 */
 //ESP_ERROR_CHECK(nvs_flash_init());
 //ESP_ERROR_CHECK(esp_netif_init());
 //ESP_ERROR_CHECK(esp_event_loop_create_default());
//Seleccionamos conexion por red wifi,y rellenamos su configuración a través del skdconfig

 //ESP_ERROR_CHECK(example_connect());

 
 xTaskCreate(get_bpm, "Get BPM", 8192, NULL, 1, NULL);

 //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }    
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG2, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    xTaskCreatePinnedToCore(&http_test_task, "http_test_task", 8192*4, NULL, 5, NULL,1);
    
}