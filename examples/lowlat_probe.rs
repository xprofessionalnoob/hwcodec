// Offline probe for the low-latency encoder options: encodes synthetic NV12 frames
// through the RAM encoder and prints frame sizes, encode times and rate peaks.
//
// cargo run --release --example lowlat_probe -- --encoder h264_vaapi --fps 60 \
//     --kbps 20000 --content scroll [--legacy] [--enc-fps 30] [--frames 600]
// Options can be varied with NEARDESK_ENC_OPTS="key=value,key=value".
use hwcodec::{
    common::{Quality, RateControl},
    ffmpeg::AVPixelFormat,
    ffmpeg_ram::encode::{EncodeContext, Encoder},
};
use std::time::Instant;

struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
}

/// Luma texture, `rows` high. text: dark glyph-like cells on white, noise: random.
fn texture(w: usize, rows: usize, seed: u64, noise: bool) -> Vec<u8> {
    let mut rng = Rng(seed | 1);
    let mut t = vec![235u8; w * rows];
    if noise {
        for p in t.iter_mut() {
            *p = 16 + (rng.next() % 220) as u8;
        }
        return t;
    }
    // lines of 16 px height, glyph cells 8x12 with random pixel patterns
    for line in 0..rows / 20 {
        let y0 = line * 20 + 4;
        let mut x = 16;
        while x + 8 < w {
            if rng.next() % 6 != 0 {
                let bits = rng.next();
                for gy in 0..12 {
                    for gx in 0..6 {
                        if (bits >> ((gy * 6 + gx) % 60)) & 1 == 1 && rng.next() % 4 != 0 {
                            t[(y0 + gy) * w + x + gx] = 24;
                        }
                    }
                }
            }
            x += 9;
        }
    }
    t
}

fn percentile(v: &mut Vec<f64>, p: f64) -> f64 {
    if v.is_empty() {
        return 0.0;
    }
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    v[(((v.len() - 1) as f64) * p).round() as usize]
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let get = |k: &str, d: &str| -> String {
        args.iter()
            .position(|a| a == k)
            .and_then(|i| args.get(i + 1).cloned())
            .unwrap_or(d.to_owned())
    };
    let name = get("--encoder", "h264_vaapi");
    let width: usize = get("--width", "1920").parse().unwrap();
    let height: usize = get("--height", "1080").parse().unwrap();
    let fps: i32 = get("--fps", "60").parse().unwrap();
    let kbps: i32 = get("--kbps", "20000").parse().unwrap();
    let frames: usize = get("--frames", "600").parse().unwrap();
    let content = get("--content", "scroll");
    // frame rate handed to the encoder, the old app always passed 30
    let enc_fps: i32 = get("--enc-fps", &fps.to_string()).parse().unwrap();
    if args.iter().any(|a| a == "--legacy") {
        std::env::set_var("NEARDESK_ENC_LEGACY", "1");
    }
    let _ = env_logger::try_init();

    let mut enc = Encoder::new(EncodeContext {
        name: name.clone(),
        mc_name: None,
        width: width as _,
        height: height as _,
        pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
        align: 64,
        fps: enc_fps,
        gop: i32::MAX,
        rc: RateControl::RC_CBR,
        quality: Quality::Quality_Default,
        kbs: kbps,
        q: -1,
        thread_count: 1,
    })
    .unwrap_or_else(|_| {
        eprintln!("cannot create encoder {name}");
        std::process::exit(2)
    });
    let (ls_y, ls_uv) = (enc.linesize[0] as usize, enc.linesize[1] as usize);
    let uv_off = enc.offset[0] as usize;
    let mut buf = vec![0u8; enc.length as usize];

    let rows = height * 2;
    let noise = content == "noise";
    let tex_a = texture(width, rows, 0x1234_5678_9abc, noise);
    let tex_b = texture(width, rows, 0x0fed_cba9_8765, noise);
    let mut uv_rng = Rng(77);
    let uv: Vec<u8> = (0..width * height / 2)
        .map(|_| 120 + (uv_rng.next() % 16) as u8)
        .collect();

    let avg = kbps as f64 * 1000.0 / 8.0 / fps as f64;
    let mut sizes: Vec<(i64, usize, bool, usize)> = vec![]; // pts, bytes, key, emitted at input index
    let mut enc_ms: Vec<f64> = vec![];
    for i in 0..frames {
        let (tex, shift) = match content.as_str() {
            "static" => (if i < frames / 2 { &tex_a } else { &tex_b }, 0),
            "noise" => (&tex_a, (i * 7) % height),
            _ => (&tex_a, (i * 3) % height),
        };
        for y in 0..height {
            let src = ((y + shift) % rows) * width;
            buf[y * ls_y..y * ls_y + width].copy_from_slice(&tex[src..src + width]);
        }
        for y in 0..height / 2 {
            buf[uv_off + y * ls_uv..uv_off + y * ls_uv + width]
                .copy_from_slice(&uv[y * width..(y + 1) * width]);
        }
        let pts = (i as i64 * 1000) / fps as i64;
        let t = Instant::now();
        match enc.encode(&buf, pts) {
            Ok(fs) => {
                enc_ms.push(t.elapsed().as_secs_f64() * 1000.0);
                for f in fs.iter() {
                    sizes.push((f.pts, f.data.len(), f.key == 1, i));
                }
            }
            Err(e) => {
                eprintln!("encode error {e} at frame {i}");
                enc_ms.push(t.elapsed().as_secs_f64() * 1000.0);
            }
        }
    }

    let mut rel: Vec<f64> = sizes.iter().map(|s| s.1 as f64 / avg).collect();
    let key_rel: Vec<f64> = sizes.iter().filter(|s| s.2).map(|s| s.1 as f64 / avg).collect();
    let mut window_max = 0.0f64;
    for (a, s) in sizes.iter().enumerate() {
        let mut sum = 0usize;
        for t in &sizes[a..] {
            if t.0 - s.0 >= 1000 {
                break;
            }
            sum += t.1;
        }
        window_max = window_max.max(sum as f64 / (kbps as f64 * 1000.0 / 8.0));
    }
    let mut lat: Vec<f64> = sizes
        .iter()
        .map(|s| s.3 as f64 - (s.0 * fps as i64 / 1000) as f64)
        .collect();
    println!("encoder {name} {width}x{height} fps {fps} kbps {kbps} content {content} frames {frames} packets {}", sizes.len());
    println!("size_p50 {:.2}", percentile(&mut rel.clone(), 0.5));
    println!("size_p95 {:.2}", percentile(&mut rel.clone(), 0.95));
    println!("size_p99 {:.2}", percentile(&mut rel.clone(), 0.99));
    println!("size_max {:.2}", percentile(&mut rel, 1.0));
    println!("rate_window_max {:.2}", window_max);
    println!("enc_ms_p50 {:.2}", percentile(&mut enc_ms.clone(), 0.5));
    println!("enc_ms_p95 {:.2}", percentile(&mut enc_ms.clone(), 0.95));
    println!("enc_ms_max {:.2}", percentile(&mut enc_ms, 1.0));
    println!("key_count {}", key_rel.len());
    println!("key_size_max {:.2}", key_rel.iter().cloned().fold(0.0, f64::max));
    println!("latency_frames_p95 {:.2}", percentile(&mut lat, 0.95));
    let total: usize = sizes.iter().map(|s| s.1).sum();
    println!(
        "mean_kbps {:.0}",
        total as f64 * 8.0 / 1000.0 / (frames as f64 / fps as f64)
    );
}
