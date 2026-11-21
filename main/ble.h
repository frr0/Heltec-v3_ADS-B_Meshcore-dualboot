#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ble_service_init(void);
bool ble_is_connected(void);
void ble_send_line(const char *line);

typedef void (*ble_rx_handler_t)(const char *cmd);
void ble_set_rx_handler(ble_rx_handler_t handler);

#endif
