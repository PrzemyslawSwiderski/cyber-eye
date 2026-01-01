/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "esp_gmf_pool.h"
#include "esp_gmf_video_enc.h"
#include "esp_video_codec_utils.h"
#include "gmf_video_pattern.h"
#include "gmf_loader_setup_defaults.h"

#define TAG "VID_ENC_TEST"

/* Fixed configuration for ESP32P4 */
#define TEST_PATTERN_WIDTH 1280
#define TEST_PATTERN_HEIGHT 720
#define TEST_VIDEO_ALIGNMENT 128
#define VIDEO_EL_MAX_STACK_SIZE (40 * 1024)

/* Streaming configuration - modify as needed */
#define STREAM_URI "rtsp://192.168.1.12:8554/live" // Example RTSP URI
// Alternative examples:
// #define STREAM_URI "udp://239.0.0.1:1234"  // UDP multicast
// #define STREAM_URI "tcp://192.168.1.100:5000"  // TCP stream

#define SAFE_FREE(ptr)       \
  do                         \
  {                          \
    if (ptr)                 \
    {                        \
      esp_gmf_oal_free(ptr); \
      ptr = NULL;            \
    }                        \
  } while (0)

/* -------------------------------------------------------------------------- */
/* Simplified data structure                                                   */
/* -------------------------------------------------------------------------- */

typedef struct
{
  /* Pipeline components */
  esp_gmf_pool_handle_t pool;
  esp_gmf_pipeline_handle_t pipe;
  esp_gmf_task_handle_t task;
  esp_gmf_element_handle_t enc_hd;

  /* Video buffers */
  uint8_t *src_pixel;
  uint32_t src_size;
  uint8_t *out_pixel;
  uint32_t out_size;
  uint32_t out_max_size;

  /* State */
  uint32_t frame_count;
  bool no_need_free;
} h264_encoder_t;

static h264_encoder_t encoder;

/* -------------------------------------------------------------------------- */
/* Pattern generation                                                          */
/* -------------------------------------------------------------------------- */

static int allocate_src_pattern(void)
{
  esp_video_codec_resolution_t res = {
      .width = TEST_PATTERN_WIDTH,
      .height = TEST_PATTERN_HEIGHT,
  };

  encoder.src_size = esp_video_codec_get_image_size(
      (esp_video_codec_pixel_fmt_t)ESP_FOURCC_OUYY_EVYY, &res);

  encoder.src_pixel = esp_gmf_oal_malloc_align(
      TEST_VIDEO_ALIGNMENT, encoder.src_size);

  if (!encoder.src_pixel)
  {
    return -1;
  }

  pattern_info_t pattern = {
      .format_id = ESP_FOURCC_OUYY_EVYY,
      .res = {.width = TEST_PATTERN_WIDTH, .height = TEST_PATTERN_HEIGHT},
      .pixel = encoder.src_pixel,
      .data_size = encoder.src_size,
      .bar_count = 8,
      .vertical = false,
  };

  gen_pattern_color_bar(&pattern);
  return 0;
}

static void free_video_buffers(void)
{
  SAFE_FREE(encoder.src_pixel);
  if (!encoder.no_need_free)
  {
    SAFE_FREE(encoder.out_pixel);
  }
  encoder.out_max_size = 0;
}

/* -------------------------------------------------------------------------- */
/* GMF IO callbacks                                                            */
/* -------------------------------------------------------------------------- */

static esp_gmf_err_io_t in_acquire(void *handle, esp_gmf_payload_t *load,
                                   uint32_t wanted_size, int wait_ticks)
{
  load->pts = encoder.frame_count * 100;
  load->buf = encoder.src_pixel;
  load->valid_size = encoder.src_size;
  load->buf_length = load->valid_size;
  return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t in_release(void *handle, esp_gmf_payload_t *load,
                                   uint32_t wanted_size, int wait_ticks)
{
  encoder.frame_count++;
  vTaskDelay(10);
  return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t out_acquire(void *handle, esp_gmf_payload_t *load,
                                    uint32_t wanted_size, int wait_ticks)
{
  if (load->buf)
  {
    encoder.no_need_free = true;
    return ESP_GMF_IO_OK;
  }

  if (wanted_size > encoder.out_max_size)
  {
    SAFE_FREE(encoder.out_pixel);
    encoder.out_pixel = esp_gmf_oal_malloc_align(
        TEST_VIDEO_ALIGNMENT, wanted_size + TEST_VIDEO_ALIGNMENT);
    encoder.out_max_size = wanted_size;
  }

  load->buf = encoder.out_pixel;
  load->buf_length = encoder.out_max_size;
  return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t out_release(void *handle, esp_gmf_payload_t *load,
                                    uint32_t wanted_size, int wait_ticks)
{
  encoder.out_size = load->valid_size;
  if (!encoder.no_need_free)
  {
    load->buf = NULL;
  }
  return ESP_GMF_IO_OK;
}

/* -------------------------------------------------------------------------- */
/* Pipeline setup and teardown                                                 */
/* -------------------------------------------------------------------------- */

static int prepare_pipeline(void)
{
  esp_gmf_pool_init(&encoder.pool);
  if (!encoder.pool)
  {
    return -1;
  }

  gmf_loader_setup_video_codec_default(encoder.pool);

  const char *elements[] = {"vid_enc", NULL};
  esp_gmf_pool_new_pipeline(encoder.pool,
                            NULL,
                            elements,
                            1,
                            NULL,
                            &encoder.pipe);

  esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(
      in_acquire, in_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
  esp_gmf_pipeline_reg_el_port(encoder.pipe,
                               "vid_enc",
                               ESP_GMF_IO_DIR_READER,
                               in_port);

  esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(
      out_acquire, out_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
  esp_gmf_pipeline_reg_el_port(encoder.pipe,
                               "vid_enc",
                               ESP_GMF_IO_DIR_WRITER,
                               out_port);

  esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
  cfg.thread.stack = VIDEO_EL_MAX_STACK_SIZE;
  cfg.thread.stack_in_ext = true;

  esp_gmf_task_init(&cfg, &encoder.task);
  esp_gmf_pipeline_bind_task(encoder.pipe, encoder.task);
  esp_gmf_pipeline_loading_jobs(encoder.pipe);

  esp_gmf_pipeline_get_el_by_name(encoder.pipe, "vid_enc", &encoder.enc_hd);

  /* Set output URI for streaming */
  esp_gmf_pipeline_set_out_uri(encoder.pipe, STREAM_URI);

  return 0;
}

static void release_pipeline(void)
{
  if (encoder.pipe)
  {
    esp_gmf_pipeline_stop(encoder.pipe);
    esp_gmf_pipeline_destroy(encoder.pipe);
  }
  if (encoder.task)
  {
    esp_gmf_task_deinit(encoder.task);
  }
  gmf_loader_teardown_video_codec_default(encoder.pool);
  esp_gmf_pool_deinit(encoder.pool);
}

/* -------------------------------------------------------------------------- */
/* Main test function                                                          */
/* -------------------------------------------------------------------------- */

void run_video_mod(void)
{
  memset(&encoder, 0, sizeof(encoder));
  ESP_ERROR_CHECK(prepare_pipeline());
  ESP_ERROR_CHECK(allocate_src_pattern());

  /* Configure H264 encoding */
  esp_gmf_info_video_t info = {
      .format_id = ESP_FOURCC_OUYY_EVYY,
      .width = TEST_PATTERN_WIDTH,
      .height = TEST_PATTERN_HEIGHT,
      .fps = 10,
  };

  esp_gmf_video_enc_set_dst_codec(encoder.enc_hd, ESP_FOURCC_H264);
  esp_gmf_pipeline_report_info(encoder.pipe, ESP_GMF_INFO_VIDEO,
                               &info, sizeof(info));

  /* Run encoding */
  ESP_ERROR_CHECK(esp_gmf_pipeline_run(encoder.pipe));
  vTaskDelay(100000);
  ESP_ERROR_CHECK(esp_gmf_pipeline_stop(encoder.pipe));

  /* Cleanup */
  free_video_buffers();
  release_pipeline();
}
