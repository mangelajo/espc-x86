// ESP-IDF version stub
#pragma once

#define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define ESP_IDF_VERSION  ESP_IDF_VERSION_VAL(5, 1, 0)

inline const char *esp_get_idf_version() { return "native-stub"; }
