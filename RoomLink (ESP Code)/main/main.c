#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include <string.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define WIFI_SSID "RoomLink" // Nombre con el que nos conectaremos al access point del ESP.
#define WIFI_PWD "Link12345" // Contraseña del ESP para poder conectarse al access point del ESP.
#define WIFI_CHANNEL 11
#define MAX_CONN_CNT 1

typedef struct {
    char ssid[32];
    char password[64];
} WifiCredentials;

WifiCredentials wifi_credentials; // Estructura para almacenar los valores recibidos por la página web

static const char *TAG = "Room"; // Tag para poder imprimir en la consola.

/*************************************************************************
 *                                HTML                                   *
 * nota: Esto lo pueden mejorar y colocarlo más bonito, pero no es       *
 * necesario, porque mientras más código, más memoria requerirá          *
 * ***********************************************************************/
static const char *HTML_FORM =
		"<!DOCTYPE html>"
		"<html>"
		"<head>"
		    "<title>Formulario de Credenciales</title>"
		"</head>"
		"<body>"
		    "<h1>Ingrese las Credenciales</h1>"
		    "<form action=\"/\" method=\"post\">"
		        "SSID: <input type=\"text\" id=\"ssid\" name=\"ssid\"><br>"
		        "Password: <input type=\"password\" id=\"password\" name=\"password\"><br>"
		        "<input type=\"submit\" value=\"Guardar\">"
		    "</form>"
		"</body>"
		"</html>";

static void start_webserver(void);
static esp_err_t handle_http_get(httpd_req_t *req);
static esp_err_t handle_http_post(httpd_req_t *req);
static void handle_wifi_events(void *, esp_event_base_t, int32_t, void *);
static esp_err_t save_credentials_post_handler(httpd_req_t *req);

static void init_wifi_ap(void)
{
	if (nvs_flash_init() != ESP_OK)
	{
		nvs_flash_erase();
		nvs_flash_init();
	}
	esp_event_loop_create_default();
	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_wifi_events, NULL);
	esp_netif_init();
	esp_netif_create_default_wifi_ap();
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);
	wifi_config_t wifi_config = {
		.ap = {
				.ssid = WIFI_SSID,
				.ssid_len = strlen(WIFI_SSID),
				.channel = WIFI_CHANNEL,
				.password = WIFI_PWD,
				.max_connection = MAX_CONN_CNT,
				.authmode = WIFI_AUTH_WPA_WPA2_PSK},
				};
	esp_wifi_set_mode(WIFI_MODE_AP);
	esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
	esp_wifi_start();
	start_webserver();
}

void app_main(void)
{
	init_wifi_ap();
	while (1)
	{
	ESP_LOGI(TAG, "%s", wifi_credentials.ssid);
	ESP_LOGI(TAG, "%s", wifi_credentials.password);
	vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

static void handle_wifi_events(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_id == WIFI_EVENT_AP_STACONNECTED)
	{
		ESP_LOGI(TAG, "a station connected");
	}
}

static void start_webserver(void)
{
	httpd_uri_t uri_get = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = handle_http_get,
			.user_ctx = NULL};
	httpd_uri_t uri_post = {
			.uri = "/",
			.method = HTTP_POST,
			.handler = save_credentials_post_handler,
			.user_ctx = NULL};
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	if (httpd_start(&server, &config) == ESP_OK)
	{
		httpd_register_uri_handler(server, &uri_get);
	 	httpd_register_uri_handler(server, &uri_post);
	}
 }

static esp_err_t handle_http_get(httpd_req_t *req)
{
	return httpd_resp_send(req, HTML_FORM, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_http_post(httpd_req_t *req)
{
	 char content[100];
	 if (httpd_req_recv(req, content, req->content_len) <= 0)
	 {
		 return ESP_FAIL;
	 }
	 ESP_LOGI(TAG, "%.*s", req->content_len, content);
	 return httpd_resp_send(req, "received", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_credentials_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret;
	int remaining = req->content_len; // Almacena el tamaño de los datos recibidos por el AP.
    int pos = 0; // Posición actual en el búfer

    while (remaining > 0) // Cuando estos datos son mayores a 0 se inicia este bucle.
    {
        /* Leer los datos recibidos del formulario */
        if ((ret = httpd_req_recv(req, buf + pos, MIN(remaining, sizeof(buf) - pos - 1))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req); // Timeout del cliente
            }
            return ESP_FAIL;
        }

        remaining -= ret;
        pos += ret;
    }

    buf[pos] = '&'; // Asegurar que el búfer esté terminado con un carácter nulo
    ESP_LOGI(TAG, "%s", buf);
    /* Procesar los datos recibidos aquí */
    // En este ejemplo, asumimos que los datos recibidos del formulario son de la forma "ssid=mi_ssid&password=mi_password"

    char *ssid_start = strstr(buf, "ssid="); // Buscar el inicio de la cadena "ssid="
    if (ssid_start)
    {
        ssid_start += 5; // Avanzar el puntero 5 posiciones para saltar la cadena "ssid="

        char *ssid_end = strstr(ssid_start, "&"); // Buscar el final de la cadena SSID (hasta el siguiente "&" en el formulario)
        if (ssid_end)
        {
            int ssid_len = ssid_end - ssid_start;
            if (ssid_len < sizeof(wifi_credentials.ssid))
            {
                strncpy(wifi_credentials.ssid, ssid_start, ssid_len); // Copiar el SSID en la variable wifi_credentials.ssid
                wifi_credentials.ssid[ssid_len] = '\0'; // Asegurar que el SSID esté terminado con un carácter nulo
            }
        }
    }

    char *password_start = strstr(buf, "password="); // Buscar el inicio de la cadena "password="
    if (password_start)
    {
        password_start += 9; // Avanzar el puntero 9 posiciones para saltar la cadena "password="

        char *password_end = strstr(password_start, "&"); // Buscar el final de la cadena de contraseña (hasta el siguiente "&" en el formulario)
        if (password_end)
        {
            int password_len = password_end - password_start;
            if (password_len < sizeof(wifi_credentials.password))
            {
                strncpy(wifi_credentials.password, password_start, password_len); // Copiar la contraseña en la variable wifi_credentials.password
                wifi_credentials.password[password_len] = '\0'; // Asegurar que la contraseña esté terminada con un carácter nulo
            }
        }
    }

    return httpd_resp_send(req, "Received", HTTPD_RESP_USE_STRLEN); // Enviar una respuesta vacía para indicar que se recibieron los datos correctamente
    return ESP_OK;
}
