extern "C" {
#include <libavutil/opt.h>
}

#include "util.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "common.h"

#include "common.h"

#define LOG_MODULE "UTIL"
#include "log.h"

namespace util_encode {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name, int kbs,
                      int gop, int fps) {
  c->has_b_frames = 0;
  c->max_b_frames = 0;
  if (gop > 0 && gop < std::numeric_limits<int16_t>::max()) {
    c->gop_size = gop;
  } else if (name.find("vaapi") != std::string::npos) {
    c->gop_size = std::numeric_limits<int16_t>::max();
  } else if (name.find("qsv") != std::string::npos) {
    c->gop_size = std::numeric_limits<uint16_t>::max();
  } else {
    c->gop_size = std::numeric_limits<int>::max();
  }
  c->keyint_min = std::numeric_limits<int>::max();
  /* put sample parameters */
  // https://github.com/FFmpeg/FFmpeg/blob/415f012359364a77e8394436f222b74a8641a3ee/libavcodec/encode.c#L581
  if (kbs > 0) {
    c->bit_rate = kbs * 1000;
    if (name.find("qsv") != std::string::npos) {
      c->rc_max_rate = c->bit_rate;
      c->bit_rate--; // cbr with vbr
    }
  }
  /* frames per second */
  c->time_base = av_make_q(1, 1000);
  c->framerate = av_make_q(fps, 1);
  c->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->slices = 1;
  c->thread_type = FF_THREAD_SLICE;
  c->thread_count = c->slices;

  // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
  c->color_range = AVCOL_RANGE_MPEG;
  c->colorspace = AVCOL_SPC_SMPTE170M;
  c->color_primaries = AVCOL_PRI_SMPTE170M;
  c->color_trc = AVCOL_TRC_SMPTE170M;

  if (name.find("h264") != std::string::npos) {
    c->profile = FF_PROFILE_H264_HIGH;
  } else if (name.find("hevc") != std::string::npos) {
    c->profile = FF_PROFILE_HEVC_MAIN;
  }
}

bool set_lantency_free(void *priv_data, const std::string &name) {
  int ret;

  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "delay", "0", 0)) < 0) {
      LOG_ERROR(std::string("nvenc set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("amf") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "query_timeout", "1000", 0)) < 0) {
      LOG_ERROR(std::string("amf set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR(std::string("qsv set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR(std::string("vaapi set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "realtime", 1, 0)) < 0) {
      LOG_ERROR(std::string("videotoolbox set realtime failed, ret = ") + av_err2str(ret));
      return false;
    }
    if ((ret = av_opt_set_int(priv_data, "prio_speed", 1, 0)) < 0) {
      LOG_ERROR(std::string("videotoolbox set prio_speed failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool set_quality(void *priv_data, const std::string &name, int quality) {
  int ret = -1;

  if (name.find("nvenc") != std::string::npos) {
    switch (quality) {
    // p7 isn't zero lantency
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "p4", 0)) < 0) {
        LOG_ERROR(std::string("nvenc set opt preset p4 failed, ret = ") + av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "p1", 0)) < 0) {
        LOG_ERROR(std::string("nvenc set opt preset p1 failed, ret = ") + av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("amf") != std::string::npos) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "quality", "quality", 0)) < 0) {
        LOG_ERROR(std::string("amf set opt quality quality failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "quality", "balanced", 0)) < 0) {
        LOG_ERROR(std::string("amf set opt quality balanced failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "quality", "speed", 0)) < 0) {
        LOG_ERROR(std::string("amf set opt quality speed failed, ret = ") + av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "preset", "veryslow", 0)) < 0) {
        LOG_ERROR(std::string("qsv set opt preset veryslow failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "medium", 0)) < 0) {
        LOG_ERROR(std::string("qsv set opt preset medium failed, ret = ") + av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "veryfast", 0)) < 0) {
        LOG_ERROR(std::string("qsv set opt preset veryfast failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("mediacodec") != std::string::npos) {
    if (name.find("h264") != std::string::npos) {
      if ((ret = av_opt_set(priv_data, "level", "5.1", 0)) < 0) {
        LOG_ERROR(std::string("mediacodec set opt level 5.1 failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
    }
    if (name.find("hevc") != std::string::npos) {
      // https:en.wikipedia.org/wiki/High_Efficiency_Video_Coding_tiers_and_levels
      if ((ret = av_opt_set(priv_data, "level", "h5.1", 0)) < 0) {
        LOG_ERROR(std::string("mediacodec set opt level h5.1 failed, ret = ") +
                  av_err2str(ret));
        return false;
      }
    }
  }
  return true;
}

struct CodecOptions {
  std::string codec_name;
  std::string option_name;
  std::map<int, std::string> rc_values;
};

bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int q) {
  if (name.find("qsv") != std::string::npos) {
    // https://github.com/LizardByte/Sunshine/blob/3e47cd3cc8fd37a7a88be82444ff4f3c0022856b/src/video.cpp#L1635
    c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
  }
  std::vector<CodecOptions> codecs = {
      {"nvenc", "rc", {{RC_CBR, "cbr"}, {RC_VBR, "vbr"}}},
      {"amf", "rc", {{RC_CBR, "cbr"}, {RC_VBR, "vbr_latency"}}},
      {"mediacodec",
       "bitrate_mode",
       {{RC_CBR, "cbr"}, {RC_VBR, "vbr"}, {RC_CQ, "cq"}}},
      // {"videotoolbox", "constant_bit_rate", {{RC_CBR, "1"}}},
    };

  for (const auto &codec : codecs) {
    if (name.find(codec.codec_name) != std::string::npos) {
      auto it = codec.rc_values.find(rc);
      if (it != codec.rc_values.end()) {
        int ret = av_opt_set(c->priv_data, codec.option_name.c_str(),
                             it->second.c_str(), 0);
        if (ret < 0) {
          LOG_ERROR(codec.codec_name + " set opt " + codec.option_name + " " +
                    it->second + " failed, ret = " + av_err2str(ret));
          return false;
        }
        if (name.find("mediacodec") != std::string::npos) {
          if (rc == RC_CQ) {
            if (q >= 0 && q <= 51) {
              c->global_quality = q;
            }
          }
        }
      }
      break;
    }
  }

  return true;
}
bool set_gpu(void *priv_data, const std::string &name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR(std::string("nvenc set gpu failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool force_hw(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "hw_encoding", 1, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set hw_encoding failed, ret = ") +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "allow_sw", 0, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set allow_sw failed, ret = ") +
                av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool set_others(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    // ff_eAVScenarioInfo_DisplayRemoting = 1
    if ((ret = av_opt_set_int(priv_data, "scenario", 1, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set scenario failed, ret = ") +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "idr_interval",
                              std::numeric_limits<int>::max(), 0)) < 0) {
      LOG_ERROR(std::string("vaapi set idr_interval failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs) {
  if (kbs > 0) {
    c->bit_rate = kbs * 1000;
    if (name.find("qsv") != std::string::npos) {
      c->rc_max_rate = c->bit_rate;
    }
  }
  return true;
}

namespace {

bool has(const std::string &name, const char *part) {
  return name.find(part) != std::string::npos;
}

bool is_gpu_encoder(const std::string &name) {
  return has(name, "nvenc") || has(name, "amf") || has(name, "qsv") ||
         has(name, "vaapi");
}

bool has_low_power(const std::string &name) {
  return has(name, "vaapi") || has(name, "qsv");
}

bool env_flag(const char *key) {
  const char *v = getenv(key);
  return v && v[0] != '\0' && strcmp(v, "0") != 0;
}

// Development override of the VBV size in frames, set by NEARDESK_ENC_OPTS.
std::atomic<double> g_vbv_frames_override{0.0};

double vbv_frames(const std::string &name) {
  double o = g_vbv_frames_override.load();
  if (o > 0.0)
    return o;
  // The VAAPI HRD does not accept a one frame buffer on every driver.
  return has(name, "vaapi") ? 1.5 : 1.0;
}

int ctx_kbs(const AVCodecContext *c) {
  // qsv uses bit_rate = kbs * 1000 - 1
  return (int)((c->bit_rate + 999) / 1000);
}

int ctx_fps(const AVCodecContext *c) {
  if (c->framerate.num > 0 && c->framerate.den > 0)
    return c->framerate.num / c->framerate.den;
  return 0;
}

// Failed options are reported and skipped, they must never stop the encoder.
bool set_opt(void *priv, const char *key, const std::string &value,
             int *failed, std::string *applied) {
  int ret = av_opt_set(priv, key, value.c_str(), 0);
  if (ret < 0) {
    LOG_WARN(std::string("set low-latency opt ") + key + "=" + value +
             " failed, ret = " + av_err2str(ret));
    if (failed)
      (*failed)++;
    return false;
  }
  if (applied)
    *applied += std::string(" ") + key + "=" + value;
  return true;
}

} // namespace

int vbv_bits(int kbs, int fps, double frames) {
  if (kbs <= 0 || fps <= 0)
    return 1;
  double bits = (double)kbs * 1000.0 / fps * frames;
  if (bits > (double)std::numeric_limits<int>::max())
    return std::numeric_limits<int>::max();
  return std::max(1, (int)std::lround(bits));
}

bool set_low_latency(AVCodecContext *c, const std::string &name, int fps,
                     bool legacy, int *failed_options, bool no_low_power) {
  if (failed_options)
    *failed_options = 0;
  if (legacy || !is_gpu_encoder(name))
    return true;

  void *priv = c->priv_data;
  const int kbs = ctx_kbs(c);
  std::string applied;
  int *failed = failed_options;
  auto vbv = [&]() {
    if (kbs > 0 && fps > 0) {
      c->rc_buffer_size = vbv_bits(kbs, fps, vbv_frames(name));
      applied += " rc_buffer_size=" + std::to_string(c->rc_buffer_size);
    }
  };

  if (has(name, "nvenc")) {
    set_opt(priv, "preset", "p4", failed, &applied);
    set_opt(priv, "tune", "ull", failed, &applied);
    set_opt(priv, "multipass", "disabled", failed, &applied);
    set_opt(priv, "zerolatency", "1", failed, &applied);
    set_opt(priv, "rc-lookahead", "0", failed, &applied);
    set_opt(priv, "forced-idr", "1", failed, &applied);
    if (c->bit_rate > 0)
      c->rc_max_rate = c->bit_rate;
    vbv();
  } else if (has(name, "amf")) {
    set_opt(priv, "usage", "ultralowlatency", failed, &applied);
    set_opt(priv, "quality", "balanced", failed, &applied);
    set_opt(priv, "filler_data", "0", failed, &applied);
    // hevc_amf calls it skip_frame
    set_opt(priv, has(name, "hevc") ? "skip_frame" : "frame_skipping", "0",
            failed, &applied);
    set_opt(priv, "preanalysis", "0", failed, &applied);
    vbv();
  } else if (has(name, "qsv")) {
    set_opt(priv, "low_power", no_low_power ? "-1" : "1", failed, &applied);
    set_opt(priv, "preset", "medium", failed, &applied);
    set_opt(priv, "forced_idr", "1", failed, &applied);
    set_opt(priv, "low_delay_brc", "1", failed, &applied);
    set_opt(priv, "scenario", "displayremoting", failed, &applied);
    // rc_max_rate and bit_rate keep the "cbr with vbr" values of set_av_codec_ctx
    vbv();
  } else if (has(name, "vaapi")) {
    if (c->bit_rate > 0) {
      // VBR capped at the bitrate: same peaks as CBR, far less on easy content
      set_opt(priv, "rc_mode", "VBR", failed, &applied);
      c->rc_max_rate = c->bit_rate;
    }
    set_opt(priv, "low_power", no_low_power ? "0" : "1", failed, &applied);
    vbv();
  }
  LOG_INFO(std::string("low-latency options for ") + name + ":" + applied);
  return true;
}

int apply_opts_string(AVCodecContext *c, const std::string &name,
                      const std::string &opts, int *failed_options) {
  std::vector<std::pair<std::string, std::string>> special;
  int applied = 0;
  size_t pos = 0;
  while (pos <= opts.size()) {
    size_t end = opts.find(',', pos);
    if (end == std::string::npos)
      end = opts.size();
    std::string item = opts.substr(pos, end - pos);
    pos = end + 1;
    size_t eq = item.find('=');
    if (eq == std::string::npos || eq == 0) {
      if (!item.empty())
        LOG_WARN(std::string("NEARDESK_ENC_OPTS: ignore '") + item + "'");
      continue;
    }
    std::string key = item.substr(0, eq);
    std::string value = item.substr(eq + 1);
    if (key == "bitrate_scale" || key == "vbv_frames" ||
        key == "max_frame_mult" || key == "gop") {
      special.emplace_back(key, value);
      continue;
    }
    int ret = av_opt_set(c->priv_data, key.c_str(), value.c_str(), 0);
    if (ret < 0)
      ret = av_opt_set(c, key.c_str(), value.c_str(), 0);
    if (ret < 0) {
      LOG_WARN(std::string("NEARDESK_ENC_OPTS: ") + key + "=" + value +
               " failed, ret = " + av_err2str(ret));
      if (failed_options)
        (*failed_options)++;
    } else {
      applied++;
    }
  }
  // special keys, always in this order
  auto find = [&](const char *key, double *out) {
    for (const auto &kv : special) {
      if (kv.first == key) {
        char *endp = nullptr;
        double v = strtod(kv.second.c_str(), &endp);
        if (endp != kv.second.c_str() && v > 0.0) {
          *out = v;
          return true;
        }
        LOG_WARN(std::string("NEARDESK_ENC_OPTS: bad value for ") + key);
        if (failed_options)
          (*failed_options)++;
      }
    }
    return false;
  };
  double v = 0.0;
  if (find("bitrate_scale", &v)) {
    c->bit_rate = (int64_t)std::llround((double)c->bit_rate * v);
    if (c->rc_max_rate > 0)
      c->rc_max_rate = (int64_t)std::llround((double)c->rc_max_rate * v);
    if (c->rc_buffer_size > 0)
      c->rc_buffer_size = (int)std::llround((double)c->rc_buffer_size * v);
    applied++;
  }
  if (find("vbv_frames", &v)) {
    g_vbv_frames_override.store(v);
    int kbs = ctx_kbs(c), fps = ctx_fps(c);
    if (kbs > 0 && fps > 0) {
      c->rc_buffer_size = vbv_bits(kbs, fps, v);
      applied++;
    }
  }
  if (find("max_frame_mult", &v)) {
    int fps = ctx_fps(c);
    if (c->bit_rate > 0 && fps > 0) {
      std::string bytes = std::to_string(
          (long long)std::llround(v * (double)c->bit_rate / 8.0 / fps));
      if (av_opt_set(c->priv_data, "max_frame_size", bytes.c_str(), 0) >= 0) {
        applied++;
      } else if (failed_options) {
        (*failed_options)++;
      }
    }
  }
  if (find("gop", &v)) {
    c->gop_size = (int)v;
    applied++;
  }
  (void)name;
  return applied;
}

void apply_env_overrides(AVCodecContext *c, const std::string &name) {
  const char *opts = getenv("NEARDESK_ENC_OPTS");
  if (!opts || opts[0] == '\0')
    return;
  int failed = 0;
  int applied = apply_opts_string(c, name, opts, &failed);
  LOG_INFO(std::string("NEARDESK_ENC_OPTS '") + opts + "' applied " +
           std::to_string(applied) + ", failed " + std::to_string(failed));
}

bool open_attempt(const std::string &name, int attempt, bool *legacy,
                  bool *no_low_power) {
  const bool env_legacy = env_flag("NEARDESK_ENC_LEGACY");
  *legacy = false;
  *no_low_power = false;
  switch (attempt) {
  case 0:
    *legacy = env_legacy;
    return true;
  case 1:
    if (env_legacy || !has_low_power(name))
      return false;
    *no_low_power = true;
    return true;
  case 2:
    if (env_legacy)
      return false;
    *legacy = true;
    return true;
  default:
    return false;
  }
}

bool open_fail_injected(int tried) {
  const char *v = getenv("NEARDESK_ENC_FORCE_OPEN_FAIL");
  if (!v || v[0] == '\0')
    return false;
  return tried < atoi(v);
}

namespace {
std::atomic<int> g_last_open_attempt{-1};
}
void set_last_open_attempt(int attempt) { g_last_open_attempt.store(attempt); }
int last_open_attempt() { return g_last_open_attempt.load(); }

bool can_force_idr(const std::string &name, bool legacy) {
  if (has(name, "vaapi"))
    return true;
  if (has(name, "nvenc") || has(name, "qsv"))
    return !legacy;
  return false;
}

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs,
                     int fps) {
  if (!change_bit_rate(c, name, kbs))
    return false;
  if (kbs <= 0 || !is_gpu_encoder(name))
    return true;
  if (fps > 0)
    c->framerate = av_make_q(fps, 1);
  if (has(name, "nvenc") || has(name, "vaapi"))
    c->rc_max_rate = c->bit_rate;
  int use_fps = ctx_fps(c);
  if (use_fps > 0)
    c->rc_buffer_size = vbv_bits(kbs, use_fps, vbv_frames(name));
  return true;
}

void vram_encode_test_callback(const uint8_t *data, int32_t len, int32_t key, const void *obj, int64_t pts) {
  (void)data;
  (void)len;
  (void)pts;
  if (obj) {
    int32_t *pkey = (int32_t *)obj;
    *pkey = key;
  }
}

} // namespace util_encode

namespace util_decode {

static bool g_flag_could_not_find_ref_with_poc = false;

bool has_flag_could_not_find_ref_with_poc() {
  bool v = g_flag_could_not_find_ref_with_poc;
  g_flag_could_not_find_ref_with_poc = false;
  return v;
}

} // namespace util_decode

extern "C" void hwcodec_set_flag_could_not_find_ref_with_poc() {
  util_decode::g_flag_could_not_find_ref_with_poc = true;
}

// Hooks for tests/lowlat_options.rs: an AVCodecContext prepared like init() does,
// without opening it. No hardware needed.
extern "C" {
void *hwcodec_test_ctx_new(const char *name, int kbs, int fps) {
  const AVCodec *codec = avcodec_find_encoder_by_name(name);
  if (!codec)
    return nullptr;
  AVCodecContext *c = avcodec_alloc_context3(codec);
  if (!c)
    return nullptr;
  std::string n(name);
  util_encode::set_av_codec_ctx(c, n, kbs, 0, fps);
  util_encode::set_lantency_free(c->priv_data, n);
  util_encode::set_rate_control(c, n, RC_CBR, -1);
  return c;
}

int hwcodec_test_ctx_apply(void *ctx, int fps, int legacy, int no_low_power) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  int failed = 0;
  util_encode::set_low_latency(c, c->codec->name, fps, legacy != 0, &failed,
                               no_low_power != 0);
  return failed;
}

int hwcodec_test_ctx_opts(void *ctx, const char *opts) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  int failed = 0;
  util_encode::apply_opts_string(c, c->codec->name, opts, &failed);
  return failed;
}

int hwcodec_test_ctx_change_bit_rate(void *ctx, int kbs, int fps) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  return util_encode::change_bit_rate(c, c->codec->name, kbs, fps) ? 0 : -1;
}

int64_t hwcodec_test_ctx_get(void *ctx, const char *field) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  std::string f(field);
  if (f == "bit_rate")
    return c->bit_rate;
  if (f == "rc_max_rate")
    return c->rc_max_rate;
  if (f == "rc_buffer_size")
    return c->rc_buffer_size;
  if (f == "gop_size")
    return c->gop_size;
  if (f == "framerate")
    return c->framerate.den > 0 ? c->framerate.num / c->framerate.den : 0;
  return -1;
}

// Reads a private option the way `ffmpeg -h encoder=...` shows it (string form).
int hwcodec_test_ctx_get_opt(void *ctx, const char *key, char *out, int len) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  uint8_t *val = nullptr;
  int ret = av_opt_get(c->priv_data, key, 0, &val);
  if (ret < 0)
    return ret;
  snprintf(out, len, "%s", (const char *)val);
  av_free(val);
  return 0;
}

void hwcodec_test_ctx_free(void *ctx) {
  AVCodecContext *c = (AVCodecContext *)ctx;
  avcodec_free_context(&c);
}

// Returns bit 0: first mark() hit, bit 1: second mark() hit (must be 0),
// bit 2: frame flagged as key after mark(), bit 3: frame still flagged after unmark().
int hwcodec_test_keyframe_cycle() {
  AVFrame *f = av_frame_alloc();
  if (!f)
    return -1;
  util_encode::KeyframeRequest kf;
  int r = 0;
  kf.request();
  if (kf.mark(f))
    r |= 1;
  if (f->pict_type == AV_PICTURE_TYPE_I && (f->flags & AV_FRAME_FLAG_KEY))
    r |= 4;
  util_encode::KeyframeRequest::unmark(f);
  if (f->pict_type != AV_PICTURE_TYPE_NONE || (f->flags & AV_FRAME_FLAG_KEY))
    r |= 8;
  if (kf.mark(f))
    r |= 2;
  av_frame_free(&f);
  return r;
}

int hwcodec_test_last_open_attempt() { return util_encode::last_open_attempt(); }
}
