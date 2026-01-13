#pragma once

#include <string>
#include <functional>
#include <memory>

#include "esp_fourcc.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "gmf_loader_setup_defaults.h"

#include "task.hpp"
#include "logger.hpp"

namespace audio
{

  enum class PlayerState
  {
    IDLE,
    PLAYING,
    STOPPED,
    ERROR
  };

  using PlayerCallback = std::function<void(PlayerState, const std::string &)>;

  class MusicPlayer
  {
  public:
    struct Config
    {
      uint32_t sample_rate;
      uint8_t channels;
      uint8_t bits_per_sample;
      uint8_t volume;

      Config()
          : sample_rate(48000),
            channels(2),
            bits_per_sample(16),
            volume(50) {}
    };

    explicit MusicPlayer(Config cfg = Config())
        : config_(cfg),
          logger_({.tag = "MusicPlayer", .level = espp::Logger::Verbosity::INFO})
    {
    }

    ~MusicPlayer()
    {
      logger_.info("Destructing");
      // stop();
      // cleanup();
    }

    bool initialize()
    {
      logger_.info("Initializing");

      if (!setup_codec())
        return false;

      if (esp_gmf_pool_init(&pool_) != ESP_OK)
        return false;

      gmf_loader_setup_io_default(pool_);
      gmf_loader_setup_audio_codec_default(pool_);
      gmf_loader_setup_audio_effects_default(pool_);

      state_ = PlayerState::IDLE;
      return true;
    }

    void play(const std::string &file)
    {
      if (state_ == PlayerState::PLAYING)
      {
        logger_.warn("Music is already playing");
        return;
      }

      if (!file_exists(file))
      {
        logger_.error("File not found: {}", file);
        set_state(PlayerState::ERROR);
        return;
      }

      current_file_ = file;

      control_task_ = std::make_unique<espp::Task>(
          espp::Task::Config{
              .callback = [this](auto &, auto &)
              {
                playback_worker();
                return true; // one-shot
              },
              .task_config = {
                  .name = "music_ctrl",
                  .stack_size_bytes = 4096,
                  .priority = 5,
                  .core_id = 0,
              }});

      control_task_->start();
    }

    void stop()
    {
      logger_.info("Stopping playback");

      // Set state first to signal playback_worker to exit
      if (state_ == PlayerState::PLAYING)
      {
        set_state(PlayerState::STOPPED);
      }

      // Wait for task to complete gracefully
      if (control_task_)
      {
        int timeout_ms = 1000;
        while (control_task_ && timeout_ms > 0)
        {
          vTaskDelay(pdMS_TO_TICKS(10));
          timeout_ms -= 10;
        }

        // Force cleanup if task didn't finish
        if (control_task_)
        {
          logger_.warn("Music task did not finish gracefully, forcing cleanup");
          control_task_.reset();
        }
      }

      cleanup_pipeline();
      set_state(PlayerState::STOPPED);
      logger_.info("Playback stopped");
    }

    void set_volume(uint8_t vol)
    {
      config_.volume = vol > 100 ? 100 : vol;
      if (codec_)
      {
        esp_codec_dev_set_out_vol(codec_, config_.volume);
      }
    }

    void set_callback(PlayerCallback cb)
    {
      callback_ = std::move(cb);
    }

    PlayerState state() const { return state_; }

  private:
    // ---------- File validation ----------

    bool file_exists(const std::string &path)
    {
      FILE *file = fopen(path.c_str(), "rb");
      if (!file)
      {
        logger_.error("Cannot open file: {}", path);
        return false;
      }
      fclose(file);
      return true;
    }

    // ---------- Codec ----------

    bool setup_codec()
    {
      esp_gmf_app_codec_info_t info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
      info.play_info.sample_rate = config_.sample_rate;
      info.play_info.channel = config_.channels;
      info.play_info.bits_per_sample = config_.bits_per_sample;

      esp_gmf_app_setup_codec_dev(&info);
      vTaskDelay(pdMS_TO_TICKS(100));

      codec_ = (esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle();
      if (!codec_)
        return false;

      esp_codec_dev_set_out_mute(codec_, false);
      esp_codec_dev_set_out_vol(codec_, config_.volume);
      return true;
    }

    // ---------- Pipeline ----------

    bool create_pipeline()
    {
      const char *els[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
      const size_t el_count = sizeof(els) / sizeof(els[0]);

      if (esp_gmf_pool_new_pipeline(
              pool_, "io_file", els, el_count, "io_codec_dev", &pipe_) != ESP_OK)
        return false;

      auto out = ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe_);
      esp_gmf_io_codec_dev_set_dev(out, codec_);

      // ----- GMF TASK (MANDATORY) -----
      esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
      cfg.thread.stack = 4096;

      if (esp_gmf_task_init(&cfg, &gmf_task_) != ESP_OK)
        return false;

      esp_gmf_pipeline_bind_task(pipe_, gmf_task_);

      if (esp_gmf_pipeline_loading_jobs(pipe_) != ESP_OK)
        return false;

      return true;
    }

    bool configure_file(const std::string &path)
    {
      esp_gmf_element_handle_t dec{};
      if (esp_gmf_pipeline_get_el_by_name(pipe_, "aud_dec", &dec) != ESP_OK)
        return false;

      esp_gmf_info_sound_t info{};
      if (ends_with(path, ".mp3"))
        info.format_id = ESP_FOURCC_MP3;
      else if (ends_with(path, ".wav"))
        info.format_id = ESP_FOURCC_WAV;
      else if (ends_with(path, ".aac"))
        info.format_id = ESP_FOURCC_AAC;
      else
        return false;

      esp_gmf_audio_dec_reconfig_by_sound_info(dec, &info);

      return esp_gmf_pipeline_set_in_uri(pipe_, path.c_str()) == ESP_OK;
    }

    // ---------- Playback worker (async) ----------

    void playback_worker()
    {
      if (!create_pipeline())
      {
        logger_.error("Failed to create pipeline");
        set_state(PlayerState::ERROR);
        cleanup_pipeline();
        return;
      }

      if (!configure_file(current_file_))
      {
        logger_.error("Failed to configure file: {}", current_file_);
        set_state(PlayerState::ERROR);
        cleanup_pipeline();
        return;
      }

      set_state(PlayerState::PLAYING);

      if (esp_gmf_pipeline_run(pipe_) != ESP_OK)
      {
        logger_.error("Failed to run pipeline");
        set_state(PlayerState::ERROR);
        cleanup_pipeline();
        return;
      }

      // Wait until finished or stopped
      while (state_ == PlayerState::PLAYING)
      {
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      cleanup_pipeline();

      if (state_ != PlayerState::STOPPED)
      {
        set_state(PlayerState::STOPPED);
      }
    }

    // ---------- Cleanup ----------

    void cleanup_pipeline()
    {
      if (pipe_)
      {
        esp_gmf_pipeline_stop(pipe_);
        esp_gmf_pipeline_destroy(pipe_);
        pipe_ = nullptr;
      }

      if (gmf_task_)
      {
        esp_gmf_task_deinit(gmf_task_);
        gmf_task_ = nullptr;
      }
    }

    void cleanup()
    {
      cleanup_pipeline();

      if (pool_)
      {
        gmf_loader_teardown_audio_effects_default(pool_);
        gmf_loader_teardown_audio_codec_default(pool_);
        gmf_loader_teardown_io_default(pool_);
        esp_gmf_pool_deinit(pool_);
        pool_ = nullptr;
      }
    }

    void set_state(PlayerState s)
    {
      state_ = s;
      if (callback_)
        callback_(s, current_file_);
    }

    static bool ends_with(const std::string &s, const char *suffix)
    {
      size_t sl = s.size();
      size_t su = strlen(suffix);
      return sl >= su && s.compare(sl - su, su, suffix) == 0;
    }

    // ---------- Members ----------

    Config config_;
    PlayerState state_{PlayerState::IDLE};
    std::string current_file_;

    esp_gmf_pool_handle_t pool_{};
    esp_gmf_pipeline_handle_t pipe_{};
    esp_gmf_task_handle_t gmf_task_{};
    esp_codec_dev_handle_t codec_{};

    std::unique_ptr<espp::Task> control_task_;
    PlayerCallback callback_;

    espp::Logger logger_;
  };

} // namespace audio
