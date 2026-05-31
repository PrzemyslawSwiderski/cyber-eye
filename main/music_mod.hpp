#pragma once
#include "esp_audio_simple_player.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

class MusicPlayerMod
{
public:
  MusicPlayerMod() : player_handle_(nullptr), playback_dev_(nullptr), is_running_(false) {}

  esp_err_t init(esp_codec_dev_handle_t playback_device)
  {
    static const char *TAG = "MusicPlayer";
    ESP_LOGI(TAG, "Initializing music player...");

    if (!playback_device)
    {
      ESP_LOGE(TAG, "Invalid playback device");
      return ESP_ERR_INVALID_ARG;
    }

    playback_dev_ = playback_device;

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
      return ESP_FAIL;
    }

    err = esp_audio_simple_player_set_event(player_handle_, event_callback, this);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to set event callback: %d", err);
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Music player initialized successfully");
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

  esp_err_t pause()
  {
    if (!player_handle_)
    {
      return ESP_FAIL;
    }

    esp_gmf_err_t err = esp_audio_simple_player_pause(player_handle_);
    if (err == ESP_OK)
    {
      ESP_LOGI("MusicPlayer", "Playback paused");
    }
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
  }

  esp_err_t resume()
  {
    if (!player_handle_)
    {
      return ESP_FAIL;
    }

    esp_gmf_err_t err = esp_audio_simple_player_resume(player_handle_);
    if (err == ESP_OK)
    {
      is_running_ = true;
      ESP_LOGI("MusicPlayer", "Playback resumed");
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

    playback_dev_ = nullptr;
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
