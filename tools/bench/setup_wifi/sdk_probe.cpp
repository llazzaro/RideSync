// Experimental link/resource probe only. No caller, upload, certificate or AP activation.
#include <esp_heap_caps.h>
#include <esp_https_server.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
// Deliberately retain the declared maximum bounded transport buffers.
volatile uint8_t frame[2048];
volatile uint8_t asset[8192];
} // namespace

extern "C" size_t ridesync_setup_wifi_probe(httpd_ssl_config_t *tls, wifi_config_t *ap, bool run) {
  frame[0] = asset[0];
  if (run && tls && ap && tls->transport_mode == HTTPD_SSL_TRANSPORT_SECURE && tls->cacert_pem &&
      tls->prvtkey_pem && ap->ap.max_connection == 1 && ap->ap.authmode == WIFI_AUTH_WPA2_PSK) {
    // Caller must provide reviewed certificate trust and unique session secrets.
    // This isolated probe lacks netif/event setup and is never a working server.
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi) == ESP_OK) {
      httpd_handle_t handle = nullptr;
      if (esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK &&
          esp_wifi_set_mode(WIFI_MODE_AP) == ESP_OK &&
          esp_wifi_set_config(WIFI_IF_AP, ap) == ESP_OK && esp_wifi_start() == ESP_OK) {
        tls->httpd.max_open_sockets = 1;
        tls->httpd.backlog_conn = 1;
        tls->httpd.stack_size = 10240;
        if (httpd_ssl_start(&handle, tls) == ESP_OK)
          httpd_ssl_stop(handle);
        esp_wifi_stop();
      }
      esp_wifi_deinit();
    }
  }
  // Measurement API compatibility only; no runtime readings are manufactured.
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return heap_caps_get_minimum_free_size(caps) + heap_caps_get_largest_free_block(caps) +
         uxTaskGetStackHighWaterMark(nullptr) + frame[0] + asset[0];
}
