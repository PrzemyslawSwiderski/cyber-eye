/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device handle definition file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stddef.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_camera.h"
#include "dev_fs_fat.h"

// Device handle array
esp_board_device_handle_t g_esp_board_device_handles[] = {
    {
        .next = &g_esp_board_device_handles[1],
        .name = "audio_dac",
        .type = "audio_codec",
        .device_handle = NULL,
        .init = dev_audio_codec_init,
        .deinit = dev_audio_codec_deinit
    },
    {
        .next = &g_esp_board_device_handles[2],
        .name = "audio_adc",
        .type = "audio_codec",
        .device_handle = NULL,
        .init = dev_audio_codec_init,
        .deinit = dev_audio_codec_deinit
    },
    {
        .next = &g_esp_board_device_handles[3],
        .name = "fs_sdcard",
        .type = "fs_fat",
        .device_handle = NULL,
        .init = dev_fs_fat_init,
        .deinit = dev_fs_fat_deinit
    },
    {
        .next = NULL,
        .name = "camera",
        .type = "camera",
        .device_handle = NULL,
        .init = dev_camera_init,
        .deinit = dev_camera_deinit
    },
};
