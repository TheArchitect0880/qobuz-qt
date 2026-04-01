use anyhow::{anyhow, Result};
use cpal::{
    traits::{DeviceTrait, HostTrait, StreamTrait},
    StreamConfig,
};
use rb::{RbConsumer, RbInspector, RbProducer, SpscRb, RB};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use symphonia::core::audio::AudioBufferRef;

const RING_BUFFER_SIZE: usize = 32 * 1024;

pub struct AudioOutput {
    _ring: SpscRb<f32>,
    ring_buf_producer: rb::Producer<f32>,
    _stream: cpal::Stream,
    pub sample_rate: u32,
    pub channels: usize,
}

impl AudioOutput {
    pub fn try_open(sample_rate: u32, channels: usize) -> Result<Self> {
        let host = cpal::default_host();
        let device = host
            .default_output_device()
            .ok_or_else(|| anyhow!("no output device"))?;

        let config = StreamConfig {
            channels: channels as u16,
            sample_rate: cpal::SampleRate(sample_rate),
            buffer_size: cpal::BufferSize::Default,
        };

        let ring = SpscRb::new(RING_BUFFER_SIZE);
        let (producer, consumer) = (ring.producer(), ring.consumer());

        let stream = device.build_output_stream(
            &config,
            move |data: &mut [f32], _| {
                let n = consumer.read(data).unwrap_or(0);
                for s in &mut data[n..] {
                    *s = 0.0;
                }
            },
            |e| eprintln!("audio stream error: {e}"),
            None,
        )?;

        stream.play()?;

        Ok(Self {
            _ring: ring,
            ring_buf_producer: producer,
            _stream: stream,
            sample_rate,
            channels,
        })
    }

    pub fn write(
        &mut self,
        decoded: AudioBufferRef<'_>,
        volume: f32,
        stop: &Arc<AtomicBool>,
    ) -> Result<()> {
        let mut sample_buf = symphonia::core::audio::SampleBuffer::<f32>::new(
            decoded.capacity() as u64,
            *decoded.spec(),
        );
        sample_buf.copy_interleaved_ref(decoded);
        let samples: Vec<f32> = sample_buf.samples().iter().map(|s| s * volume).collect();
        self.write_samples(&samples, stop)
    }

    /// Write pre-converted interleaved f32 samples directly to the ring buffer.
    /// Returns early (without error) if `stop` is set.
    pub fn write_samples(&mut self, samples: &[f32], stop: &Arc<AtomicBool>) -> Result<()> {
        let mut remaining = samples;
        while !remaining.is_empty() {
            if stop.load(Ordering::SeqCst) {
                return Ok(());
            }
            match self.ring_buf_producer.write_blocking(remaining) {
                Some(n) => remaining = &remaining[n..],
                None => break,
            }
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub fn flush(&self) {
        // Wait until the ring buffer is fully emptied by cpal
        while !self._ring.is_empty() {
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        // Give the physical DAC an extra 100ms to output the last samples
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
}
