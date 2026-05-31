#pragma once

#include "esp_audio_simple_player.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_board_manager.h"

class MusicPlayerMod
{
public:
  MusicPlayerMod() : player_handle_(nullptr), playback_dev_(nullptr), is_running_(false) {}

  esp_err_t init()
  {
    static const char *TAG = "MusicPlayer";
    ESP_LOGI(TAG, "Initializing music player...");

    // Get the audio DAC device handle
    dev_audio_codec_handles_t *play_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&play_dev_handle);
    if (play_dev_handle == NULL || play_dev_handle->codec_dev == NULL)
    {
      ESP_LOGE(TAG, "Failed to get playback handle");
      return ESP_FAIL;
    }

    playback_dev_ = play_dev_handle->codec_dev;

    // Set output volume
    esp_err_t ret = esp_codec_dev_set_out_vol(playback_dev_, 10);
    if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to set output volume");
      return ESP_FAIL;
    }

    // Configure and open the codec
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = 48000;
    fs.channel = 2;
    fs.bits_per_sample = 16;

    ret = esp_codec_dev_open(playback_dev_, &fs);
    if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to open playback codec");
      return ESP_FAIL;
    }

    // Configure player
    esp_asp_cfg_t cfg = {};
    cfg.in.cb = nullptr;
    cfg.in.user_ctx = nullptr;
    cfg.out.cb = out_data_callback;
    cfg.out.user_ctx = playback_dev_;
    cfg.task_prio = 5;
    cfg.task_stack = 6 * 1024;

    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &player_handle_);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to create player: %d", err);
      cleanup_codec();
      return ESP_FAIL;
    }

    err = esp_audio_simple_player_set_event(player_handle_, event_callback, this);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to set event callback: %d", err);
      cleanup_codec();
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Music player initialized successfully");
    return ESP_OK;
  }

  esp_err_t set_volume(int volume)
  {
    if (!playback_dev_)
    {
      ESP_LOGE("MusicPlayer", "Playback device not initialized");
      return ESP_FAIL;
    }

    esp_err_t ret = esp_codec_dev_set_out_vol(playback_dev_, volume);
    if (ret != ESP_OK)
    {
      ESP_LOGE("MusicPlayer", "Failed to set volume to %d", volume);
      return ESP_FAIL;
    }

    ESP_LOGI("MusicPlayer", "Volume set to %d", volume);
    return ESP_OK;
  }

  esp_err_t play(const char *uri = "file://sdcard/test.mp3")
  {
    if (!player_handle_)
    {
      ESP_LOGE("MusicPlayer", "Player not initialized");
      return ESP_FAIL;
    }

    esp_gmf_err_t err = esp_audio_simple_player_run(player_handle_, uri, NULL);
    if (err == ESP_OK)
    {
      is_running_ = true;
      ESP_LOGI("MusicPlayer", "Now playing: %s", uri);
    }
    else
    {
      ESP_LOGE("MusicPlayer", "Failed to play: %s (err: %d)", uri, err);
    }
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
  }

  esp_err_t stop()
  {
    if (!player_handle_)
    {
      return ESP_FAIL;
    }

    esp_gmf_err_t err = esp_audio_simple_player_stop(player_handle_);
    if (err == ESP_OK)
    {
      is_running_ = false;
      ESP_LOGI("MusicPlayer", "Playback stopped");
    }
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
  }

  esp_asp_state_t get_state()
  {
    if (!player_handle_)
    {
      return ESP_ASP_STATE_ERROR;
    }

    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(player_handle_, &state);
    if (err == ESP_OK)
    {
      return state;
    }
    return ESP_ASP_STATE_ERROR;
  }

  bool is_running() const
  {
    return is_running_;
  }

  esp_err_t deinit()
  {
    if (player_handle_)
    {
      if (is_running_)
      {
        stop();
      }
      esp_audio_simple_player_destroy(player_handle_);
      player_handle_ = nullptr;
      ESP_LOGI("MusicPlayer", "Player destroyed");
    }

    cleanup_codec();
    is_running_ = false;
    return ESP_OK;
  }

  ~MusicPlayerMod()
  {
    deinit();
  }

  // Prevent copying
  MusicPlayerMod(const MusicPlayerMod &) = delete;
  MusicPlayerMod &operator=(const MusicPlayerMod &) = delete;

private:
  void cleanup_codec()
  {
    if (playback_dev_)
    {
      esp_codec_dev_close(playback_dev_);
      playback_dev_ = nullptr;
    }
  }

  static int out_data_callback(uint8_t *data, int data_size, void *ctx)
  {
    esp_codec_dev_handle_t dev = static_cast<esp_codec_dev_handle_t>(ctx);
    if (dev)
    {
      esp_codec_dev_write(dev, data, data_size);
    }
    return 0;
  }

  static int event_callback(esp_asp_event_pkt_t *event, void *ctx)
  {
    MusicPlayerMod *self = static_cast<MusicPlayerMod *>(ctx);
    static const char *TAG = "MusicPlayer";

    if (event->type == ESP_ASP_EVENT_TYPE_STATE)
    {
      esp_asp_state_t st = ESP_ASP_STATE_NONE;
      memcpy(&st, event->payload, event->payload_size);
      ESP_LOGI(TAG, "Player state: %s", esp_audio_simple_player_state_to_str(st));

      if (st == ESP_ASP_STATE_STOPPED ||
          st == ESP_ASP_STATE_FINISHED ||
          st == ESP_ASP_STATE_ERROR)
      {
        self->is_running_ = false;
      }
    }
    else if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO)
    {
      esp_asp_music_info_t info = {};
      memcpy(&info, event->payload, event->payload_size);
      ESP_LOGI(TAG, "Music info - Sample rate: %d Hz, Channels: %d, Bits: %d",
               info.sample_rate, info.channels, info.bits);
    }
    return 0;
  }

  esp_asp_handle_t player_handle_;
  esp_codec_dev_handle_t playback_dev_;
  bool is_running_;
};
