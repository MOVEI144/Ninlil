#ifndef ESP_LOG_H
#define ESP_LOG_H
void esp_log_stub(const char *tag, const char *format, ...);
#define ESP_LOGD(...) esp_log_stub(__VA_ARGS__)
#define ESP_LOGI(...) esp_log_stub(__VA_ARGS__)
#define ESP_LOGW(...) esp_log_stub(__VA_ARGS__)
#define ESP_LOGE(...) esp_log_stub(__VA_ARGS__)
#endif
