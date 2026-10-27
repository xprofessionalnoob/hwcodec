// Option tests for the low-latency tuning (cpp/common/util.cpp).
// They only need the FFmpeg encoder tables, not a GPU. Tests that open a real
// encoder skip themselves when no VAAPI render node exists.
use hwcodec as _;
use std::{
    ffi::{c_char, c_void, CString},
    sync::Mutex,
};

extern "C" {
    fn hwcodec_test_ctx_new(name: *const c_char, kbs: i32, fps: i32) -> *mut c_void;
    fn hwcodec_test_ctx_apply(ctx: *mut c_void, fps: i32, legacy: i32, no_low_power: i32) -> i32;
    fn hwcodec_test_ctx_opts(ctx: *mut c_void, opts: *const c_char) -> i32;
    fn hwcodec_test_ctx_change_bit_rate(ctx: *mut c_void, kbs: i32, fps: i32) -> i32;
    fn hwcodec_test_ctx_get(ctx: *mut c_void, field: *const c_char) -> i64;
    fn hwcodec_test_ctx_get_opt(
        ctx: *mut c_void,
        key: *const c_char,
        out: *mut c_char,
        len: i32,
    ) -> i32;
    fn hwcodec_test_ctx_free(ctx: *mut c_void);
    fn hwcodec_test_last_open_attempt() -> i32;
}

// env vars are process wide
static ENV_LOCK: Mutex<()> = Mutex::new(());

const ENCODERS: [&str; 6] = [
    "h264_nvenc",
    "hevc_nvenc",
    "h264_amf",
    "hevc_amf",
    "h264_vaapi",
    "hevc_vaapi",
];

struct Ctx(*mut c_void);

impl Ctx {
    fn new(name: &str, kbs: i32, fps: i32) -> Self {
        let _ = env_logger::builder().is_test(true).try_init();
        let n = CString::new(name).unwrap();
        let p = unsafe { hwcodec_test_ctx_new(n.as_ptr(), kbs, fps) };
        assert!(!p.is_null(), "encoder {name} not in this FFmpeg build");
        Ctx(p)
    }
    fn apply(&self, fps: i32, legacy: bool, no_low_power: bool) -> i32 {
        unsafe { hwcodec_test_ctx_apply(self.0, fps, legacy as _, no_low_power as _) }
    }
    fn opts(&self, opts: &str) -> i32 {
        let o = CString::new(opts).unwrap();
        unsafe { hwcodec_test_ctx_opts(self.0, o.as_ptr()) }
    }
    fn change(&self, kbs: i32, fps: i32) {
        assert_eq!(unsafe { hwcodec_test_ctx_change_bit_rate(self.0, kbs, fps) }, 0);
    }
    fn get(&self, field: &str) -> i64 {
        let f = CString::new(field).unwrap();
        unsafe { hwcodec_test_ctx_get(self.0, f.as_ptr()) }
    }
    fn opt(&self, key: &str) -> String {
        let k = CString::new(key).unwrap();
        let mut buf = [0 as c_char; 64];
        let ret = unsafe { hwcodec_test_ctx_get_opt(self.0, k.as_ptr(), buf.as_mut_ptr(), 64) };
        assert!(ret >= 0, "option {key} unknown, ret {ret}");
        unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
            .to_string_lossy()
            .into_owned()
    }
}

impl Drop for Ctx {
    fn drop(&mut self) {
        unsafe { hwcodec_test_ctx_free(self.0) }
    }
}

#[test]
fn options_exist_for_every_encoder() {
    for name in ENCODERS {
        for no_low_power in [false, true] {
            let c = Ctx::new(name, 20000, 60);
            assert_eq!(c.apply(60, false, no_low_power), 0, "{name}: option rejected");
        }
    }
}

#[test]
fn nvenc_options_exist() {
    for name in ["h264_nvenc", "hevc_nvenc"] {
        let default = Ctx::new(name, 20000, 60);
        let c = Ctx::new(name, 20000, 60);
        assert_eq!(c.apply(60, false, false), 0);
        assert_ne!(c.opt("tune"), default.opt("tune"));
        assert_eq!(c.opt("zerolatency"), "true");
        assert_eq!(c.opt("forced-idr"), "true");
        assert_eq!(c.opt("rc-lookahead"), "0");
    }
}

#[test]
fn amf_options_exist() {
    for name in ["h264_amf", "hevc_amf"] {
        let default = Ctx::new(name, 20000, 60);
        let c = Ctx::new(name, 20000, 60);
        assert_eq!(c.apply(60, false, false), 0);
        assert_ne!(c.opt("usage"), default.opt("usage"));
        assert_eq!(c.opt("filler_data"), "false");
        let skip = if name.contains("hevc") { "skip_frame" } else { "frame_skipping" };
        assert_eq!(c.opt(skip), "false");
    }
}

#[test]
fn legacy_sets_nothing() {
    for name in ENCODERS {
        let default = Ctx::new(name, 20000, 60);
        let c = Ctx::new(name, 20000, 60);
        assert_eq!(c.apply(60, true, false), 0);
        assert_eq!(c.get("rc_buffer_size"), 0, "{name}");
        assert_eq!(c.get("rc_max_rate"), 0, "{name}");
        let key = if name.contains("nvenc") {
            "tune"
        } else if name.contains("amf") {
            "usage"
        } else {
            "low_power"
        };
        assert_eq!(c.opt(key), default.opt(key), "{name}");
    }
}

#[test]
fn env_overrides_parse() {
    let c = Ctx::new("h264_nvenc", 20000, 60);
    assert_eq!(c.apply(60, false, false), 0);
    let tuned = c.opt("tune");
    // an entry without '=' is ignored, not counted as a failure
    assert_eq!(c.opts("preset=p3,tune=ll,nonsense"), 0);
    assert_ne!(c.opt("tune"), tuned);
    assert_eq!(c.opts("no_such_option=1"), 1);
    // special keys
    assert_eq!(c.opts("bitrate_scale=0.5"), 0);
    assert_eq!(c.get("bit_rate"), 10_000_000);
    assert_eq!(c.get("rc_max_rate"), 10_000_000);
    assert_eq!(c.get("rc_buffer_size"), 166_667);
}

#[test]
fn vbv_follows_bitrate_and_fps() {
    let c = Ctx::new("h264_nvenc", 20000, 60);
    assert_eq!(c.apply(60, false, false), 0);
    assert_eq!(c.get("rc_buffer_size"), 333_333);
    assert_eq!(c.get("rc_max_rate"), c.get("bit_rate"));
    c.change(10000, 30);
    assert_eq!(c.get("bit_rate"), 10_000_000);
    assert_eq!(c.get("rc_max_rate"), 10_000_000);
    assert_eq!(c.get("rc_buffer_size"), 333_333);
    assert_eq!(c.get("framerate"), 30);
    c.change(10000, 60);
    assert_eq!(c.get("rc_buffer_size"), 166_667);
}

#[test]
fn vaapi_vbv_is_one_and_a_half_frames() {
    let c = Ctx::new("h264_vaapi", 20000, 60);
    assert_eq!(c.apply(60, false, false), 0);
    assert_eq!(c.get("rc_buffer_size"), 500_000); // 1.5 frames
}

#[test]
fn vaapi_sets_cbr_and_vbv() {
    for name in ["h264_vaapi", "hevc_vaapi"] {
        let c = Ctx::new(name, 20000, 60);
        assert_eq!(c.apply(60, false, false), 0);
        assert_eq!(c.get("rc_max_rate"), c.get("bit_rate"));
        assert!(c.get("rc_buffer_size") > 0);
        let reference = Ctx::new(name, 20000, 60);
        assert_eq!(reference.opts("rc_mode=CBR"), 0);
        assert_eq!(c.opt("rc_mode"), reference.opt("rc_mode"));
        assert_eq!(c.opt("low_power"), "true");
    }
}

#[test]
fn vaapi_low_power_retry() {
    let c = Ctx::new("h264_vaapi", 20000, 60);
    assert_eq!(c.apply(60, false, true), 0);
    assert_eq!(c.opt("low_power"), "false");
}

fn vaapi_device_present() -> bool {
    std::path::Path::new("/dev/dri/renderD128").exists()
}

fn open_vaapi() -> Result<hwcodec::ffmpeg_ram::encode::Encoder, ()> {
    use hwcodec::{
        common::{Quality, RateControl},
        ffmpeg::AVPixelFormat,
        ffmpeg_ram::encode::{EncodeContext, Encoder},
    };
    Encoder::new(EncodeContext {
        name: "h264_vaapi".to_owned(),
        mc_name: None,
        width: 1280,
        height: 720,
        pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
        align: 0,
        fps: 60,
        gop: i32::MAX,
        rc: RateControl::RC_CBR,
        quality: Quality::Quality_Default,
        kbs: 8000,
        q: -1,
        thread_count: 1,
    })
}

#[test]
fn fallback_keeps_encoder_available() {
    if !vaapi_device_present() {
        eprintln!("skipped: no VAAPI render node");
        return;
    }
    let _guard = ENV_LOCK.lock().unwrap();
    std::env::remove_var("NEARDESK_ENC_FORCE_OPEN_FAIL");
    assert!(open_vaapi().is_ok(), "plain open must work");
    let plain = unsafe { hwcodec_test_last_open_attempt() };
    assert!(plain <= 1, "tuned open expected, got attempt {plain}");

    std::env::set_var("NEARDESK_ENC_FORCE_OPEN_FAIL", "1");
    assert!(open_vaapi().is_ok(), "retry without low_power must work");
    assert_eq!(unsafe { hwcodec_test_last_open_attempt() }, 1);

    std::env::set_var("NEARDESK_ENC_FORCE_OPEN_FAIL", "2");
    assert!(open_vaapi().is_ok(), "legacy retry must work");
    assert_eq!(unsafe { hwcodec_test_last_open_attempt() }, 2);

    std::env::set_var("NEARDESK_ENC_FORCE_OPEN_FAIL", "3");
    assert!(open_vaapi().is_err(), "all attempts failing must fail");
    std::env::remove_var("NEARDESK_ENC_FORCE_OPEN_FAIL");
}
