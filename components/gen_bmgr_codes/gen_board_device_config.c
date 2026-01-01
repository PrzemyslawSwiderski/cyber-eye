/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_fatfs_sdcard.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"

// Device configuration structures
const static dev_audio_codec_config_t esp_bmgr_audio_dac_cfg = {
    .name = "audio_dac",
    .chip = "es8311",
    .type = "audio_codec",
    .adc_enabled = false,
    .adc_max_channel = 0,
    .adc_channel_mask = 0x3,
    .adc_channel_labels = "",
    .adc_init_gain = 0,
    .dac_enabled = true,
    .dac_max_channel = 1,
    .dac_channel_mask = 0x1,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "gpio_pa_control",
            .port = 53,
            .active_level = 1,
            .gain = 6.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 48,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_out",
            .port = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = false,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_audio_codec_config_t esp_bmgr_audio_adc_cfg = {
    .name = "audio_adc",
    .chip = "es8311",
    .type = "audio_codec",
    .adc_enabled = true,
    .adc_max_channel = 2,
    .adc_channel_mask = 0x3,
    .adc_channel_labels = "RE,FC",
    .adc_init_gain = 0,
    .dac_enabled = false,
    .dac_max_channel = 0,
    .dac_channel_mask = 0x0,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "none",
            .port = -1,
            .active_level = 0,
            .gain = 0.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 48,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_in",
            .port = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = false,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_fatfs_sdcard_config_t esp_bmgr_fs_sdcard_cfg = {
    .name = "fs_sdcard",
    .mount_point = "/sdcard",
    .vfs_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16384,
        },
    .frequency = SDMMC_FREQ_HIGHSPEED,
    .slot = SDMMC_HOST_SLOT_0,
    .bus_width = 4,
    .slot_flags = 0,
    .pins = {
            .clk = 0,
            .cmd = 0,
            .d0 = 0,
            .d1 = 0,
            .d2 = 0,
            .d3 = 0,
            .d4 = 0,
            .d5 = 0,
            .d6 = 0,
            .d7 = 0,
            .cd = -1,
            .wp = -1,
        },
    .ldo_chan_id = 4,
};

// Device descriptor array
const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "audio_dac",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_dac_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_dac_cfg),
        .init_skip = false,
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "audio_adc",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_adc_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_adc_cfg),
        .init_skip = false,
    },
    {
        .next = NULL,
        .name = "fs_sdcard",
        .type = "fatfs_sdcard",
        .cfg = &esp_bmgr_fs_sdcard_cfg,
        .cfg_size = sizeof(esp_bmgr_fs_sdcard_cfg),
        .init_skip = false,
    },
};
