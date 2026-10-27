#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <chrono>
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace util_encode {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name, int kbs,
                      int gop, int fps);
bool set_lantency_free(void *priv_data, const std::string &name);
bool set_quality(void *priv_data, const std::string &name, int quality);
bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int q);
bool set_gpu(void *priv_data, const std::string &name, int gpu);
bool force_hw(void *priv_data, const std::string &name);
bool set_others(void *priv_data, const std::string &name);

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs);
// Like change_bit_rate, but also keeps rc_max_rate/rc_buffer_size and the
// frame rate consistent. fps <= 0 keeps the current frame rate.
bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs,
                     int fps);

// NearDesk low-latency tuning (see NEARDESK.md).
#define OPEN_ATTEMPTS 3
int vbv_bits(int kbs, int fps, double frames);
// legacy = true sets nothing (old behavior, used as the last resort).
// no_low_power = true retries without low_power (vaapi, qsv).
// failed_options (may be NULL) counts av_opt_set calls that were rejected.
bool set_low_latency(AVCodecContext *c, const std::string &name, int fps,
                     bool legacy, int *failed_options = nullptr,
                     bool no_low_power = false);
// Reads NEARDESK_ENC_OPTS ("key=value,key=value"), only does something if set.
void apply_env_overrides(AVCodecContext *c, const std::string &name);
int apply_opts_string(AVCodecContext *c, const std::string &name,
                      const std::string &opts, int *failed_options = nullptr);
// attempt 0: tuned, 1: tuned without low_power, 2: legacy.
// Returns false if the attempt does not apply to this encoder.
bool open_attempt(const std::string &name, int attempt, bool *legacy,
                  bool *no_low_power);
// Test hook: NEARDESK_ENC_FORCE_OPEN_FAIL=N fails the first N open attempts.
bool open_fail_injected(int tried);
void set_last_open_attempt(int attempt);
int last_open_attempt();
void vram_encode_test_callback(const uint8_t *data, int32_t len, int32_t key, const void *obj, int64_t pts);

} // namespace util

namespace util_encode {

// One pending key frame request. mark() turns the next input frame into an IDR
// request and clears the request, unmark() resets the (reused) input frame.
struct KeyframeRequest {
  bool pending = false;
  void request() { pending = true; }
  bool mark(AVFrame *f) {
    if (!pending)
      return false;
    pending = false;
    f->pict_type = AV_PICTURE_TYPE_I;
    f->flags |= AV_FRAME_FLAG_KEY;
    return true;
  }
  static void unmark(AVFrame *f) {
    f->pict_type = AV_PICTURE_TYPE_NONE;
    f->flags &= ~AV_FRAME_FLAG_KEY;
  }
};

// Whether a forced IDR works with this encoder and option set. amf (FFmpeg 7.1.1)
// ignores pict_type, nvenc and qsv need forced-idr/forced_idr from set_low_latency.
bool can_force_idr(const std::string &name, bool legacy);

} // namespace util_encode

namespace util_decode {
    bool has_flag_could_not_find_ref_with_poc();
}

namespace util {

    inline std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    inline int64_t elapsed_ms(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count();
    }

    inline bool skip_test(const int64_t *excludedLuids, const int32_t *excludeFormats, int32_t excludeCount, int64_t currentLuid, int32_t dataFormat) {
      for (int32_t i = 0; i < excludeCount; i++) {
        if (excludedLuids[i] == currentLuid && excludeFormats[i] == dataFormat) {
          return true;
        }
      }
      return false;
    }
}


#endif
