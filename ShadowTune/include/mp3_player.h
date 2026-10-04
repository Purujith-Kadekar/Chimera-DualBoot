#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  mp3_player.h — I2S audio output via CS4344 DAC + helix MP3 decoder
// ═══════════════════════════════════════════════════════════════════════════════
//  Uses the ESP-ADF helix MP3 decoder to decode MP3 files from the SD card
//  and plays them through the CS4344 24-bit/192kHz stereo DAC via I2S.
//
//  Pin assignments (from board_config.h):
//    GPIO 9  = MCLK  (CS4344 master clock)
//    GPIO 21 = LRCLK (Word Select)
//    GPIO 38 = SDIN  (Serial Data In)
//    GPIO 42 = BCLK  (Bit Clock — note: causes on-board Green LED flicker)
//
//  The CS4344 is a 24-bit/192kHz stereo DAC that receives I2S data.
//  MCLK must be 256 × sample rate (e.g., 256 × 44100 = 11.2896 MHz).
//  The ESP32-S3 I2S peripheral generates MCLK, BCLK, and LRCLK.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <FS.h>               // File type (used by _mp3File member)
#include <SD.h>               // SD.open() / FILE_READ
#include <driver/i2s_std.h>
#include "board_config.h"
#include "crypto_buffer.h"  // largeAlloc/largeFree for PSRAM audio buffers

// ── Playback state ──────────────────────────────────────────────────────
enum class PlaybackState {
  STOPPED,
  PLAYING,
  PAUSED
};

// ── Audio format info ───────────────────────────────────────────────────
struct AudioInfo {
  int sampleRate;
  int channels;
  int bitDepth;
};

class Mp3Player {
public:
  Mp3Player() = default;

  // Initialize the I2S peripheral for the CS4344 DAC.
  // Must be called once before any playback.
  bool begin();

  // ── Playback control ────────────────────────────────────────────────
  // Open and start playing an MP3 file from the SD card.
  // filePath must be a full path (e.g., "/MP3/song.mp3").
  // startByte = resume position (0 = start from beginning).
  bool play(const char* filePath, uint32_t startByte = 0);

  // Pause the current playback.
  void pause();

  // Resume after pause.
  void resume();

  // Stop playback and close the file.
  void stop();

  // ── Volume control ──────────────────────────────────────────────────
  // Volume is 0-100. Internally mapped to the I2S DAC volume.
  void setVolume(uint8_t vol);
  uint8_t getVolume() const { return _volume; }

  // ── State queries ───────────────────────────────────────────────────
  PlaybackState state() const { return _state; }
  bool isPlaying() const { return _state == PlaybackState::PLAYING; }
  bool isPaused() const { return _state == PlaybackState::PAUSED; }

  // ── Progress ────────────────────────────────────────────────────────
  // Current playback position in bytes.
  uint32_t currentBytePos() const { return _bytePos; }

  // Total file size in bytes.
  uint32_t totalBytes() const { return _fileSize; }

  // Progress as 0-100 percentage.
  uint8_t progressPercent() const;
  void seek(uint32_t bytePos);
  uint32_t samplesDecoded() const { return _totalSamps; }
  uint32_t totalSamples() const;

  // ── Tick (must be called from the main loop) ────────────────────────
  // Feeds MP3 data to the I2S output. Non-blocking — returns immediately
  // if there's nothing to do.
  void tick();

  // ── Audio info ──────────────────────────────────────────────────────
  const AudioInfo& audioInfo() const { return _audioInfo; }

  // ── I2S cleanup ─────────────────────────────────────────────────────
  void end();

private:
  PlaybackState _state = PlaybackState::STOPPED;
  uint8_t _volume = DEFAULT_VOLUME;
  uint32_t _bytePos = 0;
  uint32_t _fileSize = 0;
  AudioInfo _audioInfo = {44100, 2, 16};
  uint32_t _totalSamps = 0;
  uint32_t _bitRate = 128000;

  // I2S driver handle
  i2s_chan_handle_t _i2sTxHandle = nullptr;

  // File handle for the current MP3 file
  File _mp3File;
  bool _fileOpen = false;

  // MP3 decode buffers (PSRAM)
  static const int MP3_READ_BUF_SIZE = 4096;     // input buffer for raw MP3 data
  static const int PCM_OUT_BUF_SIZE = 1152 * 2 * 2;  // max MP3 frame = 1152 samples × 2 channels × 2 bytes
  uint8_t* _mp3ReadBuf = nullptr;   // PSRAM-allocated read buffer
  int16_t* _pcmOutBuf = nullptr;    // PSRAM-allocated PCM output buffer

  // Helix MP3 decoder state
  void* _helixDecoder = nullptr;    // HMP3Decoder pointer
  bool _decoderInit = false;

  // Read buffer management
  int _readBufLen = 0;              // bytes in the read buffer
  int _readBufPos = 0;              // current read position in the buffer

  // Tracks what the I2S peripheral is ACTUALLY currently clocked at, so
  // reconfigureI2S() only does real work when a file's rate/channel count
  // actually differs from this (cheap early-exit on every other frame).
  // Must start matching whatever initI2S() actually configures the
  // hardware to (44100/2) so the first frame of a file at a DIFFERENT
  // rate correctly triggers a reconfigure instead of being skipped.
  int _configuredSampleRate = 44100;
  int _configuredChannels = 2;

  // After seek(), the first 1-2 decoded frames are discarded (decoded,
  // but not written to I2S) instead of played. This isn't a workaround
  // for a bug in our decoder -- it's standard MP3-player practice: Layer 3
  // frames can borrow bits from the previous frame's "bit reservoir", so
  // the very first frame decoded after landing at an arbitrary byte offset
  // is genuinely missing data it needs and will decode as an audible
  // click/glitch on any MP3 decoder, not just this one. See seek().
  int _seekFramesToDiscard = 0;

  // ── Internal methods ────────────────────────────────────────────────
  bool initI2S();
  void deinitI2S();
  bool initHelixDecoder();
  void deinitHelixDecoder();
  bool refillReadBuffer();
  bool decodeAndWrite();
  void applyVolume(int16_t* samples, int numSamples);
  uint32_t detectId3v2Size();  // returns byte size of a leading ID3v2 tag (0 if none)

  // I2S configuration helpers
  bool reconfigureI2S(int sampleRate, int channels);
};

// ── Global MP3 player instance ────────────────────────────────────────────
extern Mp3Player mp3Player;
