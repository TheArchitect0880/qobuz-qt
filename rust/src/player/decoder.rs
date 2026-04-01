use anyhow::Result;
use std::collections::VecDeque;
use std::io::{self, Read, Seek, SeekFrom};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Condvar, Mutex, OnceLock,
};

use aes::Aes128;
use ctr::cipher::{KeyIvInit, StreamCipher};
use ctr::Ctr128BE;

use symphonia::core::{
    audio::SampleBuffer,
    codecs::DecoderOptions,
    errors::Error as SymphoniaError,
    formats::{FormatOptions, SeekMode, SeekTo},
    io::{MediaSource, MediaSourceStream},
    meta::MetadataOptions,
    probe::Hint,
    units::Time,
};

use super::{output::AudioOutput, PlayerCommand, PlayerStatus, TrackInfo};

// ── Bounded Segmented streaming infrastructure ───────────────────────────────

const MAX_BUF_BYTES: usize = 4 * 1024 * 1024; // 4MB cache limit

#[derive(Clone)]
pub struct PrefetchData {
    pub buf: Arc<SharedBuf>,
    pub is_flac_flag: Arc<AtomicBool>,
    pub cancel: Arc<AtomicBool>,
    pub initial_skip_secs: f64,
}

pub enum NextAction {
    Play(TrackInfo),
    Transition(TrackInfo),
}

pub struct SharedBuf {
    queue: Mutex<VecDeque<u8>>,
    done: AtomicBool,
    condvar_read: Condvar,
    condvar_write: Condvar,
}

impl SharedBuf {
    fn new() -> Self {
        Self {
            queue: Mutex::new(VecDeque::with_capacity(1024 * 1024)),
            done: AtomicBool::new(false),
            condvar_read: Condvar::new(),
            condvar_write: Condvar::new(),
        }
    }

    fn append(&self, bytes: &[u8], cancel: &Arc<AtomicBool>) {
        let mut q = self.queue.lock().unwrap();
        let mut offset = 0;

        while offset < bytes.len() {
            if cancel.load(Ordering::Acquire) {
                break;
            }
            if q.len() >= MAX_BUF_BYTES {
                let (new_q, _) = self
                    .condvar_write
                    .wait_timeout(q, std::time::Duration::from_millis(50))
                    .unwrap();
                q = new_q;
                continue;
            }
            let take = (MAX_BUF_BYTES - q.len()).min(bytes.len() - offset);
            q.extend(&bytes[offset..offset + take]);
            offset += take;
            self.condvar_read.notify_all();
        }
    }

    fn wait_init(&self, cancel: &Arc<AtomicBool>) {
        let mut q = self.queue.lock().unwrap();
        while q.is_empty() && !self.done.load(Ordering::Acquire) && !cancel.load(Ordering::Acquire)
        {
            let (new_q, _) = self
                .condvar_read
                .wait_timeout(q, std::time::Duration::from_millis(50))
                .unwrap();
            q = new_q;
        }
    }
}

struct SegmentStreamSource {
    buf: Arc<SharedBuf>,
    cancel: Arc<AtomicBool>,
}

impl Read for SegmentStreamSource {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        let mut q = self.buf.queue.lock().unwrap();
        loop {
            if self.cancel.load(Ordering::Acquire) {
                return Err(io::Error::new(
                    io::ErrorKind::Interrupted,
                    "stream cancelled",
                ));
            }

            if !q.is_empty() {
                let n = q.len().min(out.len());
                let (s1, s2) = q.as_slices();
                let n1 = s1.len().min(n);
                out[..n1].copy_from_slice(&s1[..n1]);
                let n2 = n - n1;
                if n2 > 0 {
                    out[n1..n].copy_from_slice(&s2[..n2]);
                }
                q.drain(..n);
                self.buf.condvar_write.notify_all();
                return Ok(n);
            }

            if self.buf.done.load(Ordering::Acquire) {
                return Ok(0); // EOF
            }

            let (new_q, _) = self
                .buf
                .condvar_read
                .wait_timeout(q, std::time::Duration::from_millis(50))
                .unwrap();
            q = new_q;
        }
    }
}

impl Seek for SegmentStreamSource {
    fn seek(&mut self, _pos: SeekFrom) -> io::Result<u64> {
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "Live streams cannot be seeked directly.",
        ))
    }
}

impl MediaSource for SegmentStreamSource {
    fn is_seekable(&self) -> bool {
        false
    }
    fn byte_len(&self) -> Option<u64> {
        None
    }
}

fn http_client() -> &'static reqwest::blocking::Client {
    static CLIENT: OnceLock<reqwest::blocking::Client> = OnceLock::new();
    CLIENT.get_or_init(reqwest::blocking::Client::new)
}

fn fetch_segment(url: &str, cancel: &Arc<AtomicBool>) -> Option<Vec<u8>> {
    for attempt in 0..3 {
        if cancel.load(Ordering::Acquire) {
            return None;
        }
        if attempt > 0 {
            std::thread::sleep(std::time::Duration::from_millis(500 * attempt));
        }

        match http_client().get(url).send() {
            Ok(mut r) if r.status().is_success() => {
                let mut data =
                    Vec::with_capacity(r.content_length().unwrap_or(1024 * 1024) as usize);
                let mut buf = [0u8; 32 * 1024];
                let mut ok = true;
                loop {
                    if cancel.load(Ordering::Acquire) {
                        return None;
                    }
                    match std::io::Read::read(&mut r, &mut buf) {
                        Ok(0) => break,
                        Ok(n) => data.extend_from_slice(&buf[..n]),
                        Err(e) => {
                            eprintln!("[seg] chunk read error: {e}");
                            ok = false;
                            break;
                        }
                    }
                }
                if ok {
                    return Some(data);
                }
            }
            Ok(r) => eprintln!("[seg] HTTP {} on attempt {}", r.status(), attempt),
            Err(e) => eprintln!("[seg] fetch error on attempt {}: {}", attempt, e),
        }
    }
    None
}

fn find_dfla_blocks(data: &[u8]) -> Option<Vec<u8>> {
    let mut pos = 0usize;
    while pos + 8 <= data.len() {
        let size = match data[pos..pos + 4].try_into().ok().map(u32::from_be_bytes) {
            Some(s) if s >= 8 && pos + s as usize <= data.len() => s as usize,
            _ => break,
        };
        let t = &data[pos + 4..pos + 8];

        if t == b"dfLa" {
            let body = &data[pos + 8..pos + size];
            if body.len() > 4 {
                return Some(body[4..].to_vec());
            }
        }

        let inner = match t {
            b"moov" | b"trak" | b"mdia" | b"minf" | b"stbl" => Some(&data[pos + 8..pos + size]),
            b"stsd" => data.get(pos + 16..pos + size),
            b"fLaC" => data.get(pos + 36..pos + size),
            _ => None,
        };
        if let Some(inner) = inner {
            if let Some(r) = find_dfla_blocks(inner) {
                return Some(r);
            }
        }
        pos += size;
    }
    None
}

fn extract_flac_header(init_data: &[u8]) -> Option<Vec<u8>> {
    let blocks = find_dfla_blocks(init_data)?;
    let mut out = Vec::with_capacity(4 + blocks.len());
    out.extend_from_slice(b"fLaC");
    out.extend_from_slice(&blocks);
    Some(out)
}

const QBZ1_UUID: [u8; 16] = [
    0x3b, 0x42, 0x12, 0x92, 0x56, 0xf3, 0x5f, 0x75, 0x92, 0x36, 0x63, 0xb6, 0x9a, 0x1f, 0x52, 0xb2,
];

fn read_u32_be(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(data[offset..offset + 4].try_into().unwrap())
}
fn read_u24_be(data: &[u8], offset: usize) -> u32 {
    ((data[offset] as u32) << 16) | ((data[offset + 1] as u32) << 8) | (data[offset + 2] as u32)
}

fn decrypt_and_extract_frames(data: &mut [u8], key: Option<&[u8; 16]>) -> Vec<u8> {
    let mut frames = Vec::new();
    let mut pos = 0usize;
    while pos + 8 <= data.len() {
        let box_size = read_u32_be(data, pos) as usize;
        if box_size < 8 || pos + box_size > data.len() {
            break;
        }

        if data[pos + 4..pos + 8] == *b"uuid"
            && box_size >= 36
            && data[pos + 8..pos + 24] == QBZ1_UUID
        {
            let body = pos + 24;
            if body + 12 > data.len() {
                pos += box_size;
                continue;
            }

            let raw_offset = read_u32_be(data, body + 4) as usize;
            let num_samples = read_u24_be(data, body + 9) as usize;
            let sample_data_start = pos + raw_offset;
            let table_start = body + 12;

            let mut offset = sample_data_start;
            for i in 0..num_samples {
                let e = table_start + i * 16;
                if e + 16 > data.len() {
                    break;
                }
                let size = read_u32_be(data, e) as usize;
                let enc = u16::from_be_bytes([data[e + 6], data[e + 7]]) != 0;

                let end = offset + size;
                if end <= data.len() {
                    if enc {
                        if let Some(k) = key {
                            let mut iv = [0u8; 16];
                            iv[..8].copy_from_slice(&data[e + 8..e + 16]);
                            Ctr128BE::<Aes128>::new(k.into(), (&iv).into())
                                .apply_keystream(&mut data[offset..end]);
                        }
                    }
                    frames.extend_from_slice(&data[offset..end]);
                }
                offset += size;
            }
        }
        pos += box_size;
    }
    frames
}

fn parse_key(encryption_key: Option<&str>) -> Option<[u8; 16]> {
    match encryption_key {
        Some(hex_str) => {
            let b = hex::decode(hex_str).ok()?;
            if b.len() != 16 {
                return None;
            }
            Some(b.try_into().unwrap())
        }
        None => None,
    }
}

// Exposed externally to let C++ kickstart the download process via lib.rs prefetch request
pub fn start_prefetch(
    url_template: String,
    n_segments: u32,
    encryption_key: Option<&str>,
    start_segment: usize,
) -> PrefetchData {
    let cancel = Arc::new(AtomicBool::new(false));
    let buf = Arc::new(SharedBuf::new());
    let is_flac_flag = Arc::new(AtomicBool::new(false));
    let mut initial_skip_secs = 0.0;
    let key = parse_key(encryption_key);

    let cancel_clone = Arc::clone(&cancel);
    let buf_clone = Arc::clone(&buf);
    let is_flac_flag_clone = Arc::clone(&is_flac_flag);

    if key.is_none() && start_segment > 1 {
        initial_skip_secs = 10.0;
    }

    std::thread::spawn(move || {
        let init_url = url_template.replace("$SEGMENT$", "0");
        if let Some(init) = fetch_segment(&init_url, &cancel_clone) {
            if let Some(header) = extract_flac_header(&init) {
                eprintln!("[seg] FLAC header detected");
                buf_clone.append(&header, &cancel_clone);
                is_flac_flag_clone.store(true, Ordering::Release);
            } else {
                eprintln!("[seg] Assuming raw MP3 stream");
            }
        }

        let start = start_segment.max(1).min(n_segments as usize);

        // NEW HACK: Prepend Segment 1 for unencrypted MP4 streams if seeking past segment 1
        if key.is_none() && start > 1 {
            let url1 = url_template.replace("$SEGMENT$", "1");
            eprintln!(
                "[seg] fetching INIT segment 1 for unencrypted MP4: {}",
                url1
            );
            if let Some(init_seg) = fetch_segment(&url1, &cancel_clone) {
                buf_clone.append(&init_seg, &cancel_clone);
            }
        }
        eprintln!(
            "[seg] prefetch: start={}, n_segments={}, total segments to fetch={}",
            start,
            n_segments,
            n_segments as usize - start + 1
        );

        for seg in start..=n_segments as usize {
            if cancel_clone.load(Ordering::Acquire) {
                break;
            }
            let url = url_template.replace("$SEGMENT$", &seg.to_string());
            eprintln!("[seg] fetching segment {}: {}", seg, url);
            let mut data = match fetch_segment(&url, &cancel_clone) {
                Some(d) => d,
                None => {
                    eprintln!(
                        "[seg] segment {} fetch failed permanently, aborting stream",
                        seg
                    );
                    break;
                }
            };

            let frames = decrypt_and_extract_frames(&mut data, key.as_ref());
            buf_clone.append(&frames, &cancel_clone);
        }

        buf_clone.done.store(true, Ordering::Release);
        buf_clone.condvar_read.notify_all();
        eprintln!("[seg] download thread done");
    });

    PrefetchData {
        buf,
        is_flac_flag,
        cancel,
        initial_skip_secs,
    }
}

// ─────────────────────────────────────────────────────────────────────────────

pub fn play_track_inline(
    info: TrackInfo,
    status: &PlayerStatus,
    paused: &Arc<AtomicBool>,
    audio_output: &mut Option<AudioOutput>,
    cmd_rx: &std::sync::mpsc::Receiver<PlayerCommand>,
) -> Result<Option<NextAction>> {
    eprintln!(
        "[decoder] play_track_inline: n_segments={}, encrypted={}, url={}",
        info.n_segments,
        info.encryption_key.is_some(),
        info.url
    );

    if info.n_segments > 0 {
        play_segmented(info, status, paused, audio_output, cmd_rx)
    } else {
        play_plain(info, status, paused, audio_output, cmd_rx)
    }
}

// ── Segmented (qbz-1 FLAC/AAC) playback ─────────────────────────────────────────

fn play_segmented(
    mut info: TrackInfo,
    status: &PlayerStatus,
    paused: &Arc<AtomicBool>,
    audio_output: &mut Option<AudioOutput>,
    cmd_rx: &std::sync::mpsc::Receiver<PlayerCommand>,
) -> Result<Option<NextAction>> {
    // Qobuz explicitly chunks into 10-second segments, with the last segment being the remainder.
    // (Averaging duration/n_segments incorrectly calculates ~9.7s, leading to EOF skipping when seeking near the end)
    let segment_duration_secs: f64 = 10.0;
    eprintln!(
        "[seg] track duration={:?}, n_segments={}, segment_duration_secs={}",
        info.track.duration, info.n_segments, segment_duration_secs
    );

    let mut start_segment: usize = 1;
    let mut skip_samples: u64 = 0;
    let mut stopped = false;
    let mut prefetched_next: Option<NextAction> = None;

    // Use the passed-in prefetch buffer if available!
    let mut current_prefetch = info.prefetch_data.take();

    'restart: loop {
        let cancel;
        let buf;
        let is_flac_flag;

        if let Some(pd) = current_prefetch.take() {
            eprintln!("[seg] Instantly connecting to PRE-FETCHED audio buffer in RAM!");
            cancel = pd.cancel;
            buf = pd.buf;
            is_flac_flag = pd.is_flac_flag;
        } else {
            eprintln!("[seg] starting prefetch from segment {}", start_segment);
            let p_data = start_prefetch(
                info.url.clone(),
                info.n_segments,
                info.encryption_key.as_deref(),
                start_segment,
            );
            cancel = p_data.cancel;
            buf = p_data.buf;
            is_flac_flag = p_data.is_flac_flag;
            if p_data.initial_skip_secs > 0.0 {
                // We prepended segment 1, so skip its audio length
                skip_samples += (p_data.initial_skip_secs * 44100.0) as u64;
            }
        }

        buf.wait_init(&cancel);

        if cancel.load(Ordering::Acquire) {
            if stopped || prefetched_next.is_some() {
                break 'restart;
            }
            continue 'restart;
        }

        let source = SegmentStreamSource {
            buf: Arc::clone(&buf),
            cancel: Arc::clone(&cancel),
        };
        let mss = MediaSourceStream::new(Box::new(source), Default::default());

        let mut hint = Hint::new();
        if is_flac_flag.load(Ordering::Acquire) {
            hint.with_extension("flac");
        } else {
            hint.with_extension("mp3");
        }

        let probed = match symphonia::default::get_probe().format(
            &hint,
            mss,
            &FormatOptions::default(),
            &MetadataOptions::default(),
        ) {
            Ok(p) => p,
            Err(e) => {
                eprintln!("[seg] probe failed: {e}");
                cancel.store(true, Ordering::Release);
                break 'restart;
            }
        };

        let mut format = probed.format;
        let track = match format
            .tracks()
            .iter()
            .find(|t| t.codec_params.codec != symphonia::core::codecs::CODEC_TYPE_NULL)
        {
            Some(t) => t.clone(),
            None => {
                cancel.store(true, Ordering::Release);
                break 'restart;
            }
        };

        let track_id = track.id;
        let sample_rate = track.codec_params.sample_rate.unwrap_or(44100);
        let channels = track.codec_params.channels.map(|c| c.count()).unwrap_or(2);

        let mut decoder = match symphonia::default::get_codecs()
            .make(&track.codec_params, &DecoderOptions::default())
        {
            Ok(d) => d,
            Err(e) => {
                eprintln!("[seg] decoder init failed: {e}");
                cancel.store(true, Ordering::Release);
                break 'restart;
            }
        };

        if let Some(ao) = audio_output.as_ref() {
            if ao.sample_rate != sample_rate || ao.channels != channels {
                *audio_output = None;
            }
        }
        if audio_output.is_none() {
            match AudioOutput::try_open(sample_rate, channels) {
                Ok(ao) => *audio_output = Some(ao),
                Err(e) => {
                    eprintln!("[seg] audio open failed: {e}");
                    cancel.store(true, Ordering::Release);
                    break 'restart;
                }
            }
        }
        let ao = audio_output.as_mut().unwrap();

        let mut samples_to_skip = skip_samples * channels as u64;
        let segment_offset_secs =
            ((start_segment.saturating_sub(1)) as f64 * segment_duration_secs).round() as u64;
        eprintln!(
            "[seg] segment_offset_secs={}, samples_to_skip={}",
            segment_offset_secs, samples_to_skip
        );
        let mut total_samples_decoded: u64 = 0;

        'decode: loop {
            loop {
                // 1. Process pending commands
                loop {
                    match cmd_rx.try_recv() {
                        Ok(PlayerCommand::Pause) => {
                            paused.store(true, Ordering::SeqCst);
                            *status.state.lock().unwrap() = super::PlayerState::Paused;
                        }
                        Ok(PlayerCommand::Resume) => {
                            paused.store(false, Ordering::SeqCst);
                            *status.state.lock().unwrap() = super::PlayerState::Playing;
                        }
                        Ok(PlayerCommand::SetVolume(v)) => {
                            status.volume.store(v, Ordering::Relaxed);
                        }
                        Ok(PlayerCommand::Stop) => {
                            paused.store(false, Ordering::SeqCst);
                            *status.state.lock().unwrap() = super::PlayerState::Idle;
                            *status.current_track.lock().unwrap() = None;
                            status.position_secs.store(0, Ordering::Relaxed);
                            status.duration_secs.store(0, Ordering::Relaxed);
                            stopped = true;
                            cancel.store(true, Ordering::Release);
                            break 'decode;
                        }
                        Ok(PlayerCommand::Play(new_info)) => {
                            prefetched_next = Some(NextAction::Play(new_info));
                            cancel.store(true, Ordering::Release);
                            break 'decode;
                        }
                        Ok(PlayerCommand::QueueNext(new_info)) => {
                            prefetched_next = Some(NextAction::Transition(new_info));
                        }
                        Err(std::sync::mpsc::TryRecvError::Empty) => break,
                        Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                            stopped = true;
                            cancel.store(true, Ordering::Release);
                            break 'decode;
                        }
                    }
                }

                if stopped {
                    break 'decode;
                }

                // 2. Process Seeks
                if status.seek_requested.load(Ordering::SeqCst) {
                    status.seek_requested.store(false, Ordering::SeqCst);
                    let target_secs = status.seek_target_secs.load(Ordering::Relaxed) as f64;
                    status
                        .position_secs
                        .store(target_secs as u64, Ordering::Relaxed);

                    eprintln!(
                        "[seg] seek requested: {}s, segment_duration={}s, n_segments={}",
                        target_secs, segment_duration_secs, info.n_segments
                    );
                    cancel.store(true, Ordering::Release);
                    start_segment = ((target_secs / segment_duration_secs).floor() as usize + 1)
                        .max(1)
                        .min(info.n_segments as usize);
                    let sub_sec = target_secs % segment_duration_secs;
                    skip_samples = (sub_sec * sample_rate as f64) as u64;
                    eprintln!(
                        "[seg] start_segment={}, skip_samples={}",
                        start_segment, skip_samples
                    );
                    break 'decode;
                }

                // 3. Pause sleep cycle
                if paused.load(Ordering::SeqCst) {
                    std::thread::sleep(std::time::Duration::from_millis(10));
                    continue;
                }

                break; // Event loop complete, proceed to packet decoding
            }

            let packet = match format.next_packet() {
                Ok(p) => p,
                Err(SymphoniaError::IoError(e))
                    if e.kind() == std::io::ErrorKind::UnexpectedEof =>
                {
                    break 'decode; // Natural EOF! Return prefetched track instantly
                }
                Err(SymphoniaError::IoError(e)) if e.kind() == std::io::ErrorKind::Interrupted => {
                    break 'decode;
                }
                Err(SymphoniaError::ResetRequired) => {
                    decoder.reset();
                    continue;
                }
                Err(e) => {
                    eprintln!("[seg] format error: {e}");
                    break 'decode;
                }
            };

            if packet.track_id() != track_id {
                continue;
            }

            let decoded = match decoder.decode(&packet) {
                Ok(d) => d,
                Err(SymphoniaError::IoError(_)) => break 'decode,
                Err(_) => continue,
            };

            let frame_samples = decoded.frames() as u64;
            total_samples_decoded += frame_samples;

            let current_session_secs = total_samples_decoded / sample_rate as u64;
            status.position_secs.store(
                segment_offset_secs + current_session_secs,
                Ordering::Relaxed,
            );

            let volume = status.volume.load(Ordering::Relaxed) as f32 / 100.0;
            let rg = *status.replaygain_gain.lock().unwrap();
            let gain = (volume * rg).min(1.0);

            if samples_to_skip > 0 {
                let mut sbuf = SampleBuffer::<f32>::new(decoded.capacity() as u64, *decoded.spec());
                sbuf.copy_interleaved_ref(decoded);
                let raw = sbuf.samples();
                let frame_samples = raw.len() as u64;
                if frame_samples <= samples_to_skip {
                    samples_to_skip -= frame_samples;
                    continue;
                }
                let skip_off = samples_to_skip as usize;
                samples_to_skip = 0;
                let samples: Vec<f32> = raw[skip_off..].iter().map(|s| s * gain).collect();
                ao.write_samples(&samples, &cancel)?;
            } else {
                ao.write(decoded, gain, &cancel)?;
            }
        } // end 'decode

        if stopped {
            break 'restart;
        }

        // If we broke 'decode natively (cancel is false), it's a natural EOF.
        if !cancel.load(Ordering::Acquire) {
            break 'restart;
        }

        // If we cancelled because of a hard Play() command, break immediately.
        if let Some(NextAction::Play(_)) = prefetched_next {
            break 'restart;
        }

        // Otherwise, we cancelled because of a Seek!
        // We just continue decoding the current track. Any NextAction::Transition remains queued safely!
        continue 'restart;
    } // end 'restart

    if stopped {
        *audio_output = None;
    }

    Ok(prefetched_next)
}

// ── Plain HTTP streaming source (non-segmented tracks) ───────────────────────

const HEAD_SIZE: usize = 512 * 1024;

struct HttpStreamSource {
    url: String,
    reader: reqwest::blocking::Response,
    head: Vec<u8>,
    reader_pos: u64,
    pos: u64,
    content_length: Option<u64>,
}

impl HttpStreamSource {
    fn new(
        url: String,
        response: reqwest::blocking::Response,
        content_length: Option<u64>,
    ) -> Self {
        Self {
            url,
            reader: response,
            head: Vec::new(),
            reader_pos: 0,
            pos: 0,
            content_length,
        }
    }
}

impl Read for HttpStreamSource {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let pos = self.pos as usize;
        if pos < self.head.len() {
            let avail = self.head.len() - pos;
            let n = buf.len().min(avail);
            buf[..n].copy_from_slice(&self.head[pos..pos + n]);
            self.pos += n as u64;
            return Ok(n);
        }

        let n = self.reader.read(buf)?;
        if n > 0 {
            if self.reader_pos < HEAD_SIZE as u64 {
                let capacity = HEAD_SIZE.saturating_sub(self.head.len());
                let to_buf = n.min(capacity);
                if to_buf > 0 {
                    self.head.extend_from_slice(&buf[..to_buf]);
                }
            }
            self.reader_pos += n as u64;
            self.pos += n as u64;
        }
        Ok(n)
    }
}

impl Seek for HttpStreamSource {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        let cl = self.content_length.unwrap_or(u64::MAX);
        let target: u64 = match from {
            SeekFrom::Start(n) => n,
            SeekFrom::End(n) if n < 0 => cl.saturating_sub((-n) as u64),
            SeekFrom::End(_) => cl,
            SeekFrom::Current(n) if n >= 0 => self.pos.saturating_add(n as u64),
            SeekFrom::Current(n) => self.pos.saturating_sub((-n) as u64),
        };

        if target == self.pos {
            return Ok(self.pos);
        }

        if target < self.head.len() as u64 {
            self.pos = target;
            return Ok(self.pos);
        }

        let is_small_skip = target > self.reader_pos && (target - self.reader_pos) < 1024 * 1024;

        if is_small_skip {
            let mut remaining = target - self.reader_pos;
            while remaining > 0 {
                let mut discard = [0u8; 8192];
                let want = (remaining as usize).min(discard.len());
                match self.reader.read(&mut discard[..want]) {
                    Ok(0) => break,
                    Ok(n) => {
                        if self.reader_pos < HEAD_SIZE as u64 {
                            let capacity = HEAD_SIZE.saturating_sub(self.head.len());
                            let to_buf = n.min(capacity);
                            if to_buf > 0 {
                                self.head.extend_from_slice(&discard[..to_buf]);
                            }
                        }
                        self.reader_pos += n as u64;
                        remaining -= n as u64;
                    }
                    Err(e) => return Err(e),
                }
            }
            self.pos = self.reader_pos;
            return Ok(self.pos);
        }

        // Large skip or backward skip - use Range HTTP request
        let resp = match http_client()
            .get(&self.url)
            .header("Range", format!("bytes={}-", target))
            .send()
        {
            Ok(r) => r,
            Err(e) => return Err(io::Error::other(e.to_string())),
        };

        if resp.status() == reqwest::StatusCode::PARTIAL_CONTENT {
            self.reader = resp;
            self.reader_pos = target;
            self.pos = target;
            Ok(self.pos)
        } else if resp.status().is_success() {
            // Server ignored Range header. Draining to target.
            self.reader = resp;
            self.reader_pos = 0;
            self.pos = 0;
            let mut remaining = target;
            while remaining > 0 {
                let mut discard = [0u8; 8192];
                let want = (remaining as usize).min(discard.len());
                match self.reader.read(&mut discard[..want]) {
                    Ok(0) => break,
                    Ok(n) => {
                        if self.reader_pos < HEAD_SIZE as u64 {
                            let capacity = HEAD_SIZE.saturating_sub(self.head.len());
                            let to_buf = n.min(capacity);
                            if to_buf > 0 {
                                self.head.extend_from_slice(&discard[..to_buf]);
                            }
                        }
                        self.reader_pos += n as u64;
                        remaining -= n as u64;
                    }
                    Err(e) => return Err(e),
                }
            }
            self.pos = self.reader_pos;
            Ok(self.pos)
        } else {
            Err(io::Error::other(format!("HTTP Error {}", resp.status())))
        }
    }
}

impl MediaSource for HttpStreamSource {
    fn is_seekable(&self) -> bool {
        true
    }
    fn byte_len(&self) -> Option<u64> {
        self.content_length
    }
}

// ── Plain (non-segmented) playback ───────────────────────────────────────────

fn play_plain(
    info: TrackInfo,
    status: &PlayerStatus,
    paused: &Arc<AtomicBool>,
    audio_output: &mut Option<AudioOutput>,
    cmd_rx: &std::sync::mpsc::Receiver<PlayerCommand>,
) -> Result<Option<NextAction>> {
    eprintln!("[decoder] using plain URL streaming");
    let (response, content_length) = {
        let mut last_err = String::new();
        let mut result = None;
        for attempt in 0..3u32 {
            if attempt > 0 {
                std::thread::sleep(std::time::Duration::from_millis(500 * attempt as u64));
            }
            match http_client().get(&info.url).send() {
                Ok(resp) => {
                    if resp.status().is_success() {
                        let cl = resp.content_length();
                        result = Some((resp, cl));
                        break;
                    } else {
                        last_err = format!("HTTP {}", resp.status());
                    }
                }
                Err(e) => {
                    last_err = e.to_string();
                }
            }
        }
        result.ok_or_else(|| anyhow::anyhow!("{last_err}"))?
    };

    let source = HttpStreamSource::new(info.url.clone(), response, content_length);
    let mss = MediaSourceStream::new(Box::new(source), Default::default());

    let hint = Hint::new();
    let probed = symphonia::default::get_probe()
        .format(
            &hint,
            mss,
            &FormatOptions::default(),
            &MetadataOptions::default(),
        )
        .map_err(|e| anyhow::anyhow!("probe failed: {e}"))?;

    let mut format = probed.format;
    let track = format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != symphonia::core::codecs::CODEC_TYPE_NULL)
        .unwrap()
        .clone();

    let track_id = track.id;
    let sample_rate = track.codec_params.sample_rate.unwrap_or(44100);
    let channels = track.codec_params.channels.map(|c| c.count()).unwrap_or(2);

    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .unwrap();

    if let Some(ao) = audio_output.as_ref() {
        if ao.sample_rate != sample_rate || ao.channels != channels {
            *audio_output = None;
        }
    }
    if audio_output.is_none() {
        *audio_output = Some(AudioOutput::try_open(sample_rate, channels)?);
    }
    let ao = audio_output.as_mut().unwrap();

    let stop_flag = Arc::new(AtomicBool::new(false));
    let mut stopped = false;
    let mut prefetched_next: Option<NextAction> = None;

    'decode: loop {
        loop {
            loop {
                match cmd_rx.try_recv() {
                    Ok(PlayerCommand::Pause) => {
                        paused.store(true, Ordering::SeqCst);
                        *status.state.lock().unwrap() = super::PlayerState::Paused;
                    }
                    Ok(PlayerCommand::Resume) => {
                        paused.store(false, Ordering::SeqCst);
                        *status.state.lock().unwrap() = super::PlayerState::Playing;
                    }
                    Ok(PlayerCommand::Stop) => {
                        paused.store(false, Ordering::SeqCst);
                        *status.state.lock().unwrap() = super::PlayerState::Idle;
                        *status.current_track.lock().unwrap() = None;
                        status.position_secs.store(0, Ordering::Relaxed);
                        status.duration_secs.store(0, Ordering::Relaxed);
                        stopped = true;
                        stop_flag.store(true, Ordering::SeqCst);
                        break 'decode;
                    }
                    Ok(PlayerCommand::Play(info)) => {
                        paused.store(false, Ordering::SeqCst);
                        prefetched_next = Some(NextAction::Play(info));
                        stop_flag.store(true, Ordering::SeqCst);
                        break 'decode;
                    }
                    Ok(PlayerCommand::QueueNext(info)) => {
                        prefetched_next = Some(NextAction::Transition(info));
                    }
                    Ok(PlayerCommand::SetVolume(v)) => {
                        status.volume.store(v, Ordering::Relaxed);
                    }
                    Err(std::sync::mpsc::TryRecvError::Empty) => break,
                    Err(_) => {
                        stopped = true;
                        break 'decode;
                    }
                }
            }

            if stopped {
                break 'decode;
            }

            if status.seek_requested.load(Ordering::SeqCst) {
                status.seek_requested.store(false, Ordering::SeqCst);
                let target = status.seek_target_secs.load(Ordering::Relaxed);
                status.position_secs.store(target, Ordering::Relaxed);

                let seeked = format.seek(
                    SeekMode::Coarse,
                    SeekTo::Time {
                        time: Time::from(target),
                        track_id: None,
                    },
                );
                if let Ok(s) = seeked {
                    let actual = s.actual_ts / sample_rate as u64;
                    status.position_secs.store(actual, Ordering::Relaxed);
                }
                decoder.reset();
                // Just completed a seek. We must continue 'decode loop, which starts by checking events!
                continue 'decode;
            }

            if paused.load(Ordering::SeqCst) {
                std::thread::sleep(std::time::Duration::from_millis(10));
                continue;
            }

            break; // break the event loop to read a packet
        } // End event loop

        let packet = match format.next_packet() {
            Ok(p) => p,
            Err(_) => break 'decode, // EOF
        };

        if packet.track_id() != track_id {
            continue;
        }
        if let Some(ts) = packet.ts().checked_div(sample_rate as u64) {
            status.position_secs.store(ts, Ordering::Relaxed);
        }

        if let Ok(decoded) = decoder.decode(&packet) {
            let volume = status.volume.load(Ordering::Relaxed) as f32 / 100.0;
            let rg = *status.replaygain_gain.lock().unwrap();
            let _ = ao.write(decoded, (volume * rg).min(1.0), &stop_flag);
        }
    }

    if stopped {
        *audio_output = None;
    }
    Ok(prefetched_next)
}
