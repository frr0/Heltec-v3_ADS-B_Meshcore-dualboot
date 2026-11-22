/*
 * Dual-boot bootloader for Heltec WiFi LoRa 32 V3 (ESP32-S3)
 * Allows switching between MeshCore (ota_0) and ADS-B Mode S (ota_1).
 */
#include <stdbool.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_periph.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

static const char *TAG = "boot";

static int select_partition_number(bootloader_state_t *bs);
static int selected_boot_partition(const bootloader_state_t *bs);

void __attribute__((noreturn)) call_start_cpu0(void)
{
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    bootloader_utility_load_boot_image_from_deep_sleep();
#endif

    bootloader_state_t bs = {0};
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    bootloader_utility_load_boot_image(&bs, boot_index);
}

static int select_partition_number(bootloader_state_t *bs)
{
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    return selected_boot_partition(bs);
}

static int selected_boot_partition(const bootloader_state_t *bs)
{
    int boot_index = bootloader_utility_get_selected_boot_partition(bs);
    if (boot_index == INVALID_INDEX) {
        return boot_index;
    }

    if (esp_rom_get_reset_reason(0) != RESET_REASON_CORE_DEEP_SLEEP) {
        // Power on VEXT (GPIO 36 active LOW) and bring SX1262 out of reset (GPIO 12 HIGH)
        esp_rom_gpio_pad_select_gpio(36);
        gpio_ll_output_enable(&GPIO, 36);
        gpio_ll_set_level(&GPIO, 36, 0); // VEXT power ON

        esp_rom_gpio_pad_select_gpio(12);
        gpio_ll_output_enable(&GPIO, 12);
        gpio_ll_set_level(&GPIO, 12, 1); // SX1262 out of reset
        esp_rom_delay_us(10000);

        // Setup PRG button (GPIO 0) as input with pullup
        esp_rom_gpio_pad_select_gpio(0);
        PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[0]);
        esp_rom_gpio_pad_pullup_only(0);

        // Setup onboard White LED (GPIO 35) as visual indicator
        esp_rom_gpio_pad_select_gpio(35);
        gpio_ll_output_enable(&GPIO, 35);
        gpio_ll_set_level(&GPIO, 35, 1); // Turn ON LED to indicate selection window

        const char *def_name = (boot_index == 1) ? "ADS-B Mode S" : "MeshCore";
        const char *alt_name = (boot_index == 1) ? "MeshCore" : "ADS-B Mode S";

        esp_rom_printf("\n======================================================\n");
        esp_rom_printf("  HELTEC V3 DUAL-BOOT: MeshCore & ADS-B Mode S\n");
        esp_rom_printf("  [LED ON for 2.5s] Press PRG (GPIO 0) to switch!\n");
        esp_rom_printf("  Default target: %s (slot %d)\n", def_name, boot_index);
        esp_rom_printf("  Alternate target: %s\n", alt_name);
        esp_rom_printf("======================================================\n");

        uint32_t start_ms = esp_log_early_timestamp();
        bool pressed = false;
        while ((esp_log_early_timestamp() - start_ms) < 2500) {
            if (gpio_ll_get_level(&GPIO, 0) == 0) {
                pressed = true;
                break;
            }
            esp_rom_delay_us(10000);
        }

        gpio_ll_set_level(&GPIO, 35, 0); // Turn OFF LED

        if (pressed) {
            // Invert selection
            if (boot_index == 0) {
                if (bs->app_count > 1) {
                    boot_index = 1;
                } else if (bs->test.offset != 0) {
                    boot_index = TEST_APP_INDEX;
                }
                esp_rom_printf("[DUAL-BOOT] PRG PRESSED! Switching to ADS-B Mode S...\n\n");
            } else {
                boot_index = 0;
                esp_rom_printf("[DUAL-BOOT] PRG PRESSED! Switching to MeshCore...\n\n");
            }
        } else {
            esp_rom_printf("[DUAL-BOOT] Timeout reached. Booting %s...\n\n", def_name);
        }
    }

    return boot_index;
}

struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
