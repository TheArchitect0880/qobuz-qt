pub mod decoder;
pub mod output;

use std::sync::{
    atomic::{AtomicBool, AtomicU64, AtomicU8, Ordering},
    Arc,
};
use std::time::Duration;

use crate::api::TrackDto;
use decoder::NextAction;

#[derive(Clone)]
pub enum PlayerCommand {
    Play(TrackInfo),
    QueueNext(TrackInfo),
    Pause,
    Resume,
    Stop,
    SetVolume(u8),
}

#[derive(Clone)]
pub struct TrackInfo {
    pub track: TrackDto,
    pub url: String,
    pub n_segments: u32,
    pub encryption_key: Option<String>,
    pub replaygain_db: Option<f64>,
    pub prefetch_data: Option<decoder::PrefetchData>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PlayerState {
    Idle,
    Playing,
    Paused,
    Error(String),
}

#[derive(Clone)]
pub struct PlayerStatus {
    pub state: Arc<std::sync::Mutex<PlayerState>>,
    pub position_secs: Arc<AtomicU64>,
    pub duration_secs: Arc<AtomicU64>,
    pub volume: Arc<AtomicU8>,
    pub current_track: Arc<std::sync::Mutex<Option<TrackDto>>>,
    pub track_finished: Arc<AtomicBool>,
    pub track_transitioned: Arc<AtomicBool>,
    pub seek_requested: Arc<AtomicBool>,
    pub seek_target_secs: Arc<AtomicU64>,
    pub replaygain_gain: Arc<std::sync::Mutex<f32>>,
    pub gapless: Arc<AtomicBool>,
}

impl PlayerStatus {
    pub fn new() -> Self {
        Self {
            state: Arc::new(std::sync::Mutex::new(PlayerState::Idle)),
            position_secs: Arc::new(AtomicU64::new(0)),
            duration_secs: Arc::new(AtomicU64::new(0)),
            volume: Arc::new(AtomicU8::new(80)),
            current_track: Arc::new(std::sync::Mutex::new(None)),
            track_finished: Arc::new(AtomicBool::new(false)),
            track_transitioned: Arc::new(AtomicBool::new(false)),
            seek_requested: Arc::new(AtomicBool::new(false)),
            seek_target_secs: Arc::new(AtomicU64::new(0)),
            replaygain_gain: Arc::new(std::sync::Mutex::new(1.0)),
            gapless: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn get_state(&self) -> PlayerState {
        self.state.lock().unwrap().clone()
    }
    pub fn get_position(&self) -> u64 {
        self.position_secs.load(Ordering::Relaxed)
    }
    pub fn get_duration(&self) -> u64 {
        self.duration_secs.load(Ordering::Relaxed)
    }
    pub fn get_volume(&self) -> u8 {
        self.volume.load(Ordering::Relaxed)
    }
}

pub struct Player {
    pub cmd_tx: std::sync::mpsc::SyncSender<PlayerCommand>,
    pub status: PlayerStatus,
}

impl Player {
    pub fn new() -> Self {
        let (cmd_tx, cmd_rx) = std::sync::mpsc::sync_channel(32);
        let status = PlayerStatus::new();
        let status_clone = status.clone();

        std::thread::spawn(move || {
            player_loop(cmd_rx, status_clone);
        });

        Self { cmd_tx, status }
    }

    pub fn send(&self, cmd: PlayerCommand) {
        self.cmd_tx.send(cmd).ok();
    }
    pub fn pause(&self) {
        self.send(PlayerCommand::Pause);
    }
    pub fn resume(&self) {
        self.send(PlayerCommand::Resume);
    }
    pub fn stop(&self) {
        self.send(PlayerCommand::Stop);
    }
    pub fn set_volume(&self, vol: u8) {
        self.status.volume.store(vol, Ordering::Relaxed);
        self.send(PlayerCommand::SetVolume(vol));
    }
    pub fn seek(&self, secs: u64) {
        self.status.seek_target_secs.store(secs, Ordering::Relaxed);
        self.status.seek_requested.store(true, Ordering::SeqCst);
    }
}

fn player_loop(rx: std::sync::mpsc::Receiver<PlayerCommand>, status: PlayerStatus) {
    use std::sync::mpsc::RecvTimeoutError;

    let mut audio_output: Option<output::AudioOutput> = None;
    let paused = Arc::new(AtomicBool::new(false));
    let mut pending_action: Option<NextAction> = None;

    'outer: loop {
        let info = if let Some(action) = pending_action.take() {
            match action {
                NextAction::Play(info) | NextAction::Transition(info) => info,
            }
        } else {
            loop {
                match rx.recv_timeout(Duration::from_millis(100)) {
                    Ok(PlayerCommand::Play(info)) => break info,
                    Ok(PlayerCommand::QueueNext(info)) => {
                        // If we are completely idle and get QueueNext, treat as Play
                        break info;
                    }
                    Ok(PlayerCommand::Stop) => {
                        audio_output = None;
                        paused.store(false, Ordering::SeqCst);
                        *status.state.lock().unwrap() = PlayerState::Idle;
                        *status.current_track.lock().unwrap() = None;
                        status.position_secs.store(0, Ordering::Relaxed);
                        status.duration_secs.store(0, Ordering::Relaxed);
                    }
                    Ok(PlayerCommand::SetVolume(v)) => {
                        status.volume.store(v, Ordering::Relaxed);
                    }
                    Ok(_) => {} // Ignore Pause/Resume when idle
                    Err(RecvTimeoutError::Timeout) => {}
                    Err(RecvTimeoutError::Disconnected) => break 'outer,
                }
            }
        };

        let rg_factor = info
            .replaygain_db
            .map(|db| 10f32.powf(db as f32 / 20.0))
            .unwrap_or(1.0);
        *status.replaygain_gain.lock().unwrap() = rg_factor;

        *status.state.lock().unwrap() = PlayerState::Playing;
        *status.current_track.lock().unwrap() = Some(info.track.clone());
        if let Some(dur) = info.track.duration {
            status.duration_secs.store(dur as u64, Ordering::Relaxed);
        }
        status.position_secs.store(0, Ordering::Relaxed);
        paused.store(false, Ordering::SeqCst);

        // TrackInfo now directly passes the prefetch_data (if it exists) to the decoder
        match decoder::play_track_inline(info, &status, &paused, &mut audio_output, &rx) {
            Ok(Some(NextAction::Play(next_track))) => {
                pending_action = Some(NextAction::Play(next_track));
                // Interrupted by a manual play, no need to tell C++ to advance the queue
            }
            Ok(Some(NextAction::Transition(next_track))) => {
                pending_action = Some(NextAction::Play(next_track));
                status.track_transitioned.store(true, Ordering::SeqCst);
            }
            Ok(None) => {
                if !status.gapless.load(Ordering::Relaxed) {
                    audio_output = None;
                }
                *status.state.lock().unwrap() = PlayerState::Idle;
                status.track_finished.store(true, Ordering::SeqCst);
            }
            Err(e) => {
                eprintln!("playback error: {e}");
                *status.state.lock().unwrap() = PlayerState::Error(e.to_string());
                status.track_finished.store(true, Ordering::SeqCst);
            }
        }
    }
}
