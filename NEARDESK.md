# NearDesk changes to hwcodec

Base: rustdesk-org/hwcodec `ec38eba`. All new encoder settings are fixed defaults,
there is no user-facing switch. Every change below is one commit.

## Low-latency option scaffold, retry on open, probe (Task 3.3)

- `util_encode::set_low_latency()` (cpp/common/util.cpp) sets the per-vendor options,
  `apply_env_overrides()` reads `NEARDESK_ENC_OPTS` (`key=value,key=value`, only
  active when set; special keys `bitrate_scale`, `vbv_frames`, `max_frame_mult`, `gop`).
  Order in both `init()`: set_av_codec_ctx, set_lantency_free, set_rate_control,
  set_others, set_low_latency, apply_env_overrides, avcodec_open2.
- Failed `av_opt_set` calls are logged (warn) and skipped.
- Retry: if `avcodec_open2` fails, the encoder is rebuilt with the tuned options but
  without `low_power` (vaapi, qsv only), and then with the old options (legacy).
  Test hook `NEARDESK_ENC_FORCE_OPEN_FAIL=N` fails the first N open attempts,
  `NEARDESK_ENC_LEGACY=1` forces the old options (used by `examples/lowlat_probe.rs --legacy`).
- One info line per encoder start lists the options that were set.
- `examples/lowlat_probe.rs` encodes synthetic 1080p NV12 and prints size
  percentiles (in multiples of the average frame), `rate_window_max` (largest 1 s
  window / target), encode time, key frame size, output delay in frames.
- `tests/lowlat_options.rs`: option tests without GPU (the Linux FFmpeg contains
  nvenc, amf and vaapi encoders, not qsv). The open/fallback test skips itself
  without `/dev/dri/renderD128`.

## Real frame rate and matching VBV (Task 3.4)

- `change_bit_rate(c, name, kbs, fps)` also sets `rc_max_rate` (nvenc, vaapi) and
  `rc_buffer_size = vbv_bits(kbs, fps, frames)`. `frames` is 1.0, vaapi 1.5.
- New FFI `ffmpeg_ram_set_framerate`, `Encoder::set_framerate` (ram), the vram
  `set_framerate` now also updates the VBV.
- Limit: NVENC reads the frame rate only at init. A later `set_framerate` changes
  the VBV size, not the per-frame budget of the encoder. QSV can update it.

## VAAPI (Task 3.5)

- `rc_mode=CBR`, `rc_max_rate = bit_rate`, `rc_buffer_size = 1.5 frames`, `low_power=1`
  (retry with 0). Before: AVBR/VBR with a one second HRD buffer.
- `hevc_vaapi` stays off. `NEARDESK_ENC_HEVC_VAAPI=1` adds it to `available_encoders()`
  for the investigation. Decision needs M6 at equal bitrate and a viewer that decodes
  HEVC via VAAPI; not decided here (see probe numbers: hevc_vaapi behaves like h264_vaapi).
- Probe, Intel UHD (Raptor Lake-P, iHD driver), h264_vaapi, 1080p60, 20 Mbit/s, 600 frames:

  | Variant | content | size_p99 | rate_window_max | enc_ms_p95 | key_size_max | mean kbit/s |
  |---|---|---|---|---|---|---|
  | V0a old app (legacy, encoder told 30 fps) | scroll | 2.53 | 2.58 | 5.40 | 6.58 | 39135 |
  | V0 old options, 60 fps | scroll | 1.26 | 1.19 | 5.99 | 3.95 | 19322 |
  | V1 CBR + VBV 1.5 | scroll | 1.06 | 1.02 | 6.25 | 2.34 | 20048 |
  | V2 V1 + low_power=1 (default) | scroll | 1.06 | 1.02 | 6.09 | 2.34 | 20048 |
  | V3 V2 + max_frame_size 6x | scroll | 1.06 | 1.02 | 6.19 | 2.34 | 20048 |
  | V4 V2 + blbrc=1 | scroll | 1.75 | 1.06 | 7.62 | 2.58 | 20224 |
  | V0a | noise | 3.03 | 2.55 | 6.28 | 6.34 | 38460 |
  | V0 | noise | 2.00 | 1.11 | 6.26 | 4.04 | 18726 |
  | V1 | noise | 1.43 | 1.02 | 6.23 | 1.46 | 20027 |
  | V2 | noise | 1.43 | 1.02 | 5.95 | 1.46 | 20027 |
  | V3 | noise | 1.43 | 1.02 | 6.05 | 1.46 | 20027 |
  | V4 | noise | 3.17 | 1.18 | 8.19 | 1.72 | 21314 |

  V2 is the default (V3 gives nothing, V4 is worse). The "V0a" row is the behaviour of
  the app before Task 3.4: the encoder believed 30 fps at 60 fps and spent twice the
  bitrate. `low_power=1` made no measurable difference to encode time on this GPU
  (the open succeeded, no fallback).
  VBV size (scroll / noise p99): 1 frame 1.00 / 1.00, 1.5 frames 1.06 / 1.43,
  2 frames 1.22 / 1.59; the key frame was not larger with 1 frame. 1.5 stays as planned.
- Side effect of CBR: with perfectly static content CBR keeps spending the full bitrate
  (static, second half: 20046 kbit/s vs 9114 legacy). `rc_mode=VBR` with
  `rc_max_rate = bit_rate` keeps the peaks (`rate_window_max` 0.99-1.02) and uses far less
  on easy content (static 587 kbit/s, noise hevc 3310 kbit/s, scroll 18545 kbit/s);
  `AVBR` is rejected by the driver. The app only encodes when the capture changes, so
  CBR is kept as planned; VBR is the alternative if idle bandwidth matters.
