#ifndef HELTEC_BATTERY_H
#define HELTEC_BATTERY_H

#include "esp_err.h"

esp_err_t battery_init(void);
int battery_read_mv(void);
int battery_percent(int mv);

#endif
