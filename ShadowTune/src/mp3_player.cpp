// ═══════════════════════════════════════════════════════════════════════════════
//  mp3_player.cpp — I2S audio output via CS4344 DAC + helix MP3 decoder
// ═══════════════════════════════════════════════════════════════════════════════
//  Decodes MP3 files from the SD card using the ESP-ADF helix decoder and
//  plays them through the CS4344 24-bit/192kHz stereo DAC via I2S (standard
//  mode, not PDM/TDM).
//
//  The CS4344 expects MCLK = 256 × sample_rate. The ESP32-S3 I2S peripheral
//  generates MCLK, BCLK, and LRCLK automatically from the configured clock
//  tree.
//
//  Pin assignments (from board_config.h):
//    GPIO 9  = MCLK  (CS4344 master clock)
//    GPIO 21 = LRCLK (Word Select)
//    GPIO 38 = SDIN  (Serial Data In)
//    GPIO 42 = BCLK  (Bit Clock)
//
//  Volume is applied in software by scaling PCM samples before writing to
//  I2S. Range: 0–100, mapped to a 0.0–1.0 multiplier.
//
//  Audio buffers are allocated in PSRAM via largeAlloc() (crypto_buffer.h)
//  to preserve internal SRAM for DMA/crypto operations.
// ═══════════════════════════════════════════════════════════════════════════════

#include "mp3_player.h"
#include "mp3_decoder.h"   // Helix MP3 decoder API (HMP3Decoder API)
#include "gpio_config_manager.h"
#include "sd_manager.h"  // runtime PIN_I2S_* variables
#include <string.h>
#include <math.h>   // powf() — perceptual (log) volume taper

// ── Global instance ──────────────────────────────────────────────────────────
Mp3Player mp3Player;

// ── begin() ──────────────────────────────────────────────────────────────────
bool Mp3Player::begin() {
  Serial.println("[Mp3Player] begin()");

  // Allocate PSRAM buffers before any playback starts
  if (!_mp3ReadBuf) {
    _mp3ReadBuf = static_cast<uint8_t*>(largeAlloc(MP3_READ_BUF_SIZE));
    if (!_mp3ReadBuf) {
      Serial.println("[Mp3Player] ERROR: failed to allocate MP3 read buffer");
      return false;
    }
  }
  if (!_pcmOutBuf) {
    _pcmOutBuf = static_cast<int16_t*>(largeAlloc(PCM_OUT_BUF_SIZE));
    if (!_pcmOutBuf) {
      Serial.println("[Mp3Player] ERROR: failed to allocate PCM output buffer");
      largeFree(_mp3ReadBuf);
      _mp3ReadBuf = nullptr;
      return false;
    }
  }

  // Initialize I2S peripheral
  if (!initI2S()) {
    Serial.println("[Mp3Player] ERROR: I2S init failed");
    return false;
  }

  // Initialize the helix decoder
  if (!initHelixDecoder()) {
    Serial.println("[Mp3Player] ERROR: helix decoder init failed");
    deinitI2S();
    return false;
  }

  Serial.println("[Mp3Player] ready (I2S + helix decoder)");
  return true;
}

// ── play() ───────────────────────────────────────────────────────────────────
bool Mp3Player::play(const char* filePath, uint32_t startByte) {
  if (!filePath) return false;

  // Stop any current playback first
  stop();

  // Switch to MP3 partition before opening file
  sd_switch_to_mp3();

  // Open the MP3 file
  _mp3File = SD.open(filePath, FILE_READ);
  if (!_mp3File) {
    Serial.printf("[Mp3Player] ERROR: failed to open %s\n", filePath);
    return false;
  }
  _fileOpen = true;
  _fileSize = _mp3File.size();

  // Detect a leading ID3v2 tag (present on virtually every real-world MP3 —
  // added by iTunes/foobar2000/etc). Without this, the first decodeAndWrite()
  // call fails on the tag bytes ("ID3..." is not a valid frame sync), and the
  // error-recovery path used to only skip forward 1 byte per tick() call —
  // tick() runs once per main-loop iteration, so a multi-KB tag took many
  // seconds (matches the "15-20s before audio starts" symptom exactly).
  // Skipping it here is a single seek instead of thousands of loop iterations.
  uint32_t id3Size = detectId3v2Size();

  // Seek to resume position if requested
  if (startByte > 0 && startByte < _fileSize) {
    _mp3File.seek(startByte);
    _bytePos = startByte;
    // Same bit-reservoir artifact as seek() (see there) -- a saved resume
    // position is just as much an "arbitrary byte offset" as a manual
    // seek, so it gets the same first-frames-discarded treatment.
    _seekFramesToDiscard = 2;
  } else {
    uint32_t startPos = (id3Size > 0 && id3Size < _fileSize) ? id3Size : 0;
    if (startPos > 0) {
      _mp3File.seek(startPos);
      Serial.printf("[Mp3Player] skipped %u-byte ID3v2 tag\n", (unsigned)startPos);
    }
    _bytePos = startPos;
    _seekFramesToDiscard = 0;  // starting from the real top of the stream — no artifact
  }

  // Reset read buffer state
  _readBufLen = 0;
  _readBufPos = 0;

  // Reset decoder state — re-init to flush any leftover internal state
  // from a previous file. This is important because the helix decoder
  // retains sync state from the previous stream.
  deinitHelixDecoder();
  if (!initHelixDecoder()) {
    Serial.println("[Mp3Player] ERROR: failed to re-init helix decoder");
    _mp3File.close();
    _fileOpen = false;
    return false;
  }

  _state = PlaybackState::PLAYING;

  // ── Prime the DMA buffer before returning ───────────────────────────
  // Every caller of play() does a synchronous screen redraw right after
  // this returns (drawNowPlayingScreen() etc. -- a full-screen SPI paint
  // that can take tens of ms). If we only queued audio once the main
  // loop got back around to tick(), that redraw time is exactly the
  // "takes a while to actually start playing after I press Play" delay.
  // Decoding a handful of frames here means sound is already sitting in
  // the (now much larger, see initI2S()) DMA buffer by the time this
  // function returns, so playback is audible as soon as the loop resumes
  // -- the screen paint happens in parallel with already-queued audio,
  // not before the first sample is even decoded.
  for (int i = 0; i < 6; i++) {
    if (!decodeAndWrite()) break;
  }

  Serial.printf("[Mp3Player] playing %s  (size=%u, startByte=%u)\n",
                filePath, (unsigned)_fileSize, (unsigned)startByte);
  return true;
}

// ── pause() ──────────────────────────────────────────────────────────────────
void Mp3Player::pause() {
  if (_state == PlaybackState::PLAYING) {
    _state = PlaybackState::PAUSED;
    Serial.println("[Mp3Player] paused");
  }
}

// ── resume() ─────────────────────────────────────────────────────────────────
void Mp3Player::resume() {
  if (_state == PlaybackState::PAUSED) {
    _state = PlaybackState::PLAYING;
    Serial.println("[Mp3Player] resumed");
  }
}

// ── stop() ───────────────────────────────────────────────────────────────────
void Mp3Player::stop() {
  if (_state == PlaybackState::STOPPED && !_fileOpen) return;

  _state = PlaybackState::STOPPED;

  if (_fileOpen) {
    _mp3File.close();
    _fileOpen = false;
  }

  _bytePos = 0;
  _fileSize = 0;
  _readBufLen = 0;
  _readBufPos = 0;
  _totalSamps = 0;
  _seekFramesToDiscard = 0;

  // Reset audio info to defaults
  _audioInfo = {44100, 2, 16};

  Serial.println("[Mp3Player] stopped");
}

// ── setVolume() ──────────────────────────────────────────────────────────────
void Mp3Player::setVolume(uint8_t vol) {
  if (vol > 100) vol = 100;
  _volume = vol;
}

// ── progressPercent() ────────────────────────────────────────────────────────
uint8_t Mp3Player::progressPercent() const {
  if (_fileSize == 0) return 0;
  // Avoid 32-bit overflow: cast to uint64_t for the multiply
  uint32_t pct = static_cast<uint32_t>(
      (static_cast<uint64_t>(_bytePos) * 100) / _fileSize);
  if (pct > 100) pct = 100;
  return static_cast<uint8_t>(pct);
}

// ── seek() ──
void Mp3Player::seek(uint32_t bytePos) {
  if (!_fileOpen || _fileSize == 0) return;
  if (bytePos >= _fileSize) bytePos = _fileSize - 1;
  _mp3File.seek(bytePos); _bytePos = bytePos;
  _readBufLen = 0; _readBufPos = 0;
  deinitHelixDecoder(); initHelixDecoder();

  // Flush the DMA buffer immediately. Without this, whatever pre-seek
  // audio was already queued (up to ~270ms worth, see initI2S()) keeps
  // draining out AFTER seek() returns, so what's actually heard is:
  // [tail end of the old position] -> [click] -> [new position] instead
  // of a clean jump. Disabling then re-enabling the channel discards its
  // DMA buffer contents.
  if (_i2sTxHandle) {
    i2s_channel_disable(_i2sTxHandle);
    i2s_channel_enable(_i2sTxHandle);
  }

  // Discard (decode, but don't play) the first couple of frames after
  // landing at this new, arbitrary byte offset. This isn't papering over
  // a bug: MP3 Layer 3 frames can borrow bits from the previous frame's
  // "bit reservoir", so the very first frame decoded right after a seek
  // is often genuinely missing data it needs and decodes as an audible
  // click/glitch -- on any MP3 decoder, not specific to this one. Two
  // frames (~50ms of silence instead) reliably clears it.
  _seekFramesToDiscard = 2;

  // Estimate decoded samples from byte position (for time display)
  if (_bitRate > 0) {
    _totalSamps = (uint32_t)((uint64_t)bytePos * _audioInfo.sampleRate * _audioInfo.channels / (_bitRate / 8));
  } else {
    _totalSamps = 0;
  }
}

uint32_t Mp3Player::totalSamples() const {
  if (_fileSize == 0 || _bitRate == 0) return 0;
  return (_fileSize / (_bitRate / 8)) * _audioInfo.sampleRate * _audioInfo.channels;
}

// ── tick() ───────────────────────────────────────────────────────────────────
//  Decode multiple frames per tick to keep DMA buffer full (prevents buzzing).
void Mp3Player::tick() {
  if (_state != PlaybackState::PLAYING) return;
  if (!_fileOpen || !_decoderInit) return;

  // Decode exactly 1 frame per tick.
  // i2s_channel_write() with portMAX_DELAY will block until the DMA
  // buffer has space — this naturally paces the decode to match
  // real-time playback speed (no skipping, no buzzing).
  if (!decodeAndWrite()) {
    Serial.println("[Mp3Player] decode/write failed or EOF — stopping");
    stop();
  }
}

// ── end() ────────────────────────────────────────────────────────────────────
void Mp3Player::end() {
  stop();
  deinitHelixDecoder();
  deinitI2S();

  // Free PSRAM buffers
  if (_mp3ReadBuf) {
    largeFree(_mp3ReadBuf);
    _mp3ReadBuf = nullptr;
  }
  if (_pcmOutBuf) {
    largeFree(_pcmOutBuf);
    _pcmOutBuf = nullptr;
  }

  Serial.println("[Mp3Player] end() — resources released");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — I2S initialization
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Player::initI2S() {
  if (_i2sTxHandle) {
    Serial.println("[Mp3Player] I2S already initialized");
    return true;
  }

  // ── Channel configuration ──────────────────────────────────────────────
  // Create a TX channel on I2S port 0 (the default I2S peripheral on ESP32-S3).
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  // ── Bigger DMA buffer + auto_clear — fixes underrun noise/pops ─────────
  // The macro default is 6 descriptors x 240 frames (~33ms of audio at
  // 44.1kHz). That's razor-thin: a single full-screen SPI redraw (320x240
  // @ 40MHz plus dozens of fillRect/fillCircle/text calls for a playlist
  // screen) can easily block the main loop for longer than that, starving
  // the DMA mid-playback -- audible as noise/distortion whenever the UI
  // redraws while a track is playing (e.g. browsing the list).
  // 12 x 1000 = 12000 frames =~ 272ms of buffered audio -- comfortably
  // absorbs even a heavy multi-row list redraw without an underrun.
  // (1000 frames, not 1024: each DMA descriptor's buffer is capped at
  // 4092 bytes by the ESP32 GDMA hardware. At 16-bit stereo that's 4
  // bytes/frame, so 1024 frames = 4096 bytes would exceed the limit;
  // 1000 frames = 4000 bytes stays safely under it.)
  chanCfg.dma_desc_num = 12;
  chanCfg.dma_frame_num = 1000;
  // auto_clear: when the DMA DOES run dry (e.g. right after pause(), when
  // tick() stops feeding it), have the peripheral transmit silence (zeros)
  // instead of its default behavior of re-transmitting the last buffer's
  // contents on a loop. Re-transmitting stale audio is exactly the
  // "weird sound on pause" symptom -- this makes underrun be silent
  // instead of garbled, whether or not the bigger buffer above ever
  // actually gets exhausted.
  chanCfg.auto_clear = true;

  esp_err_t err = i2s_new_channel(&chanCfg, &_i2sTxHandle, nullptr);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] i2s_new_channel failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // ── Standard-mode configuration for CS4344 ─────────────────────────────
  // CS4344: 24-bit DAC, MCLK = 256 × fs, I2S data format (MSB-first,
  // left-justified with one BCLK delay on LRCLK edge).
  i2s_std_config_t stdCfg = {
    .clk_cfg = {
      .sample_rate_hz = 44100,
      .clk_src = I2S_CLK_SRC_DEFAULT,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)PIN_I2S_MCLK,     // GPIO 9
      .bclk = (gpio_num_t)PIN_I2S_BCLK,     // GPIO 42
      .ws   = (gpio_num_t)PIN_I2S_LRCLK,    // GPIO 21
      .dout = (gpio_num_t)PIN_I2S_SDIN,     // GPIO 38
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };

  err = i2s_channel_init_std_mode(_i2sTxHandle, &stdCfg);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] i2s_channel_init_std_mode failed: %s\n", esp_err_to_name(err));
    i2s_del_channel(_i2sTxHandle);
    _i2sTxHandle = nullptr;
    return false;
  }

  // Enable the channel — ready to receive data
  err = i2s_channel_enable(_i2sTxHandle);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] i2s_channel_enable failed: %s\n", esp_err_to_name(err));
    i2s_del_channel(_i2sTxHandle);
    _i2sTxHandle = nullptr;
    return false;
  }

  Serial.println("[Mp3Player] I2S initialized (44100 Hz, 16-bit stereo, MCLK=256×fs)");
  return true;
}

// ── deinitI2S() ──────────────────────────────────────────────────────────────
void Mp3Player::deinitI2S() {
  if (!_i2sTxHandle) return;

  i2s_channel_disable(_i2sTxHandle);
  i2s_del_channel(_i2sTxHandle);
  _i2sTxHandle = nullptr;

  Serial.println("[Mp3Player] I2S deinitialized");
}

// ── reconfigureI2S() ─────────────────────────────────────────────────────────
//  Reconfigure the I2S peripheral when the MP3 stream's sample rate or
//  channel count differs from what the hardware is currently clocked at
//  (e.g. a 16kHz mono voice-encoded file, vs. the usual 44.1kHz stereo).
//
//  This used to be a deliberate no-op ("reconfiguring breaks audio" per
//  the old comment) -- the I2S peripheral stayed hardcoded at 44100Hz
//  stereo no matter what. That meant any file not natively at 44.1kHz
//  stereo was decoded to CORRECT PCM at its real rate, then physically
//  clocked out at 44100Hz regardless -- e.g. a 16kHz file plays back at
//  44100/16000 = ~2.76x speed, badly pitched up and garbled. That's the
//  root cause of "a 64kbps/16kHz file doesn't decode properly" -- it WAS
//  decoding correctly, just being played out the DAC at the wrong rate.
//
//  i2s_channel_reconfig_std_clock()/_slot() are the correct, lightweight
//  ESP-IDF v5.x calls for this: they only touch the clock tree / slot
//  width on an already-created channel, instead of tearing down and
//  recreating the whole channel (i2s_del_channel + i2s_new_channel),
//  which is heavier and was presumably what "breaks audio" in the old
//  comment referred to.
bool Mp3Player::reconfigureI2S(int sampleRate, int channels) {
  if (!_i2sTxHandle) return false;
  if (sampleRate <= 0) return false;

  // Cheap early-exit: within one file, every frame has the same rate/
  // channel count (MP3 doesn't change sample rate mid-stream), so after
  // the first frame of a track this check is true on every subsequent
  // call and reconfigureI2S() does nothing.
  if (sampleRate == _configuredSampleRate && channels == _configuredChannels) {
    return true;
  }

  esp_err_t err = i2s_channel_disable(_i2sTxHandle);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] reconfigureI2S: disable failed: %s\n", esp_err_to_name(err));
    return false;
  }

  i2s_std_clk_config_t clkCfg = {
    .sample_rate_hz = (uint32_t)sampleRate,
    .clk_src = I2S_CLK_SRC_DEFAULT,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
  };
  err = i2s_channel_reconfig_std_clock(_i2sTxHandle, &clkCfg);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] reconfigureI2S: clock reconfig failed: %s\n", esp_err_to_name(err));
    i2s_channel_enable(_i2sTxHandle);  // best-effort: leave it usable at the old rate
    return false;
  }

  if (channels != _configuredChannels) {
    i2s_slot_mode_t slotMode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_slot_config_t slotCfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slotMode);
    err = i2s_channel_reconfig_std_slot(_i2sTxHandle, &slotCfg);
    if (err != ESP_OK) {
      Serial.printf("[Mp3Player] reconfigureI2S: slot reconfig failed: %s\n", esp_err_to_name(err));
    }
  }

  err = i2s_channel_enable(_i2sTxHandle);
  if (err != ESP_OK) {
    Serial.printf("[Mp3Player] reconfigureI2S: re-enable failed: %s\n", esp_err_to_name(err));
    return false;
  }

  _configuredSampleRate = sampleRate;
  _configuredChannels = channels;
  Serial.printf("[Mp3Player] I2S reconfigured: %d Hz, %d ch\n", sampleRate, channels);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — Helix MP3 decoder
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Player::initHelixDecoder() {
  if (_decoderInit) return true;

  // Allocate the helix decoder instance.
  // MP3InitDecoder() allocates internal working memory and returns an
  // opaque handle (HMP3Decoder = void*).
  _helixDecoder = MP3InitDecoder();
  if (!_helixDecoder) {
    Serial.println("[Mp3Player] MP3InitDecoder() failed");
    return false;
  }

  _decoderInit = true;
  Serial.println("[Mp3Player] helix decoder initialized");
  return true;
}

// ── deinitHelixDecoder() ─────────────────────────────────────────────────────
void Mp3Player::deinitHelixDecoder() {
  if (!_decoderInit) return;

  if (_helixDecoder) {
    MP3FreeDecoder(_helixDecoder);
    _helixDecoder = nullptr;
  }
  _decoderInit = false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — ID3v2 tag detection
// ═══════════════════════════════════════════════════════════════════════════════
//  ID3v2 header layout (10 bytes, at the very start of the file):
//    bytes 0-2: "ID3"
//    byte  3-4: version (major, minor)
//    byte  5:   flags — bit 4 (0x10) = footer present (adds another 10 bytes)
//    bytes 6-9: tag size, "syncsafe" 28-bit big-endian (7 bits used per byte)
//  Returns the total number of bytes to skip (header + tag body + optional
//  footer), or 0 if the file doesn't start with an ID3v2 tag. Does not
//  permanently move the file position — caller decides where to seek.
uint32_t Mp3Player::detectId3v2Size() {
  uint8_t hdr[10];
  _mp3File.seek(0);
  int n = _mp3File.read(hdr, sizeof(hdr));
  _mp3File.seek(0);  // restore — caller seeks explicitly based on our return value
  if (n < 10) return 0;
  if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') return 0;

  uint32_t size = (static_cast<uint32_t>(hdr[6] & 0x7F) << 21) |
                  (static_cast<uint32_t>(hdr[7] & 0x7F) << 14) |
                  (static_cast<uint32_t>(hdr[8] & 0x7F) << 7)  |
                   static_cast<uint32_t>(hdr[9] & 0x7F);

  uint32_t total = 10 + size;
  if (hdr[5] & 0x10) total += 10;  // footer flag
  return total;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — Read buffer management
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Player::refillReadBuffer() {
  if (!_fileOpen) return false;

  // If there's leftover data at the end of the buffer, shift it to the front
  // so we can fill the rest with fresh data from the file.
  int leftover = _readBufLen - _readBufPos;
  if (leftover > 0 && _readBufPos > 0) {
    memmove(_mp3ReadBuf, _mp3ReadBuf + _readBufPos, leftover);
  }
  _readBufPos = 0;
  _readBufLen = leftover;

  // Read from the SD card to fill the rest of the buffer
  int space = MP3_READ_BUF_SIZE - _readBufLen;
  if (space > 0) {
    int bytesRead = _mp3File.read(_mp3ReadBuf + _readBufLen, space);
    if (bytesRead > 0) {
      _readBufLen += bytesRead;
      _bytePos += bytesRead;
    }
  }

  // Return true if there's any data available
  return (_readBufLen > 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — Decode one MP3 frame and write PCM to I2S
// ═══════════════════════════════════════════════════════════════════════════════
bool Mp3Player::decodeAndWrite() {
  if (!_decoderInit || !_fileOpen) return false;

  // Make sure the read buffer has data
  if (_readBufPos >= _readBufLen) {
    if (!refillReadBuffer()) {
      // No more data — end of file
      Serial.println("[Mp3Player] EOF — no more data to read");
      return false;
    }
  }

  // Try to decode one MP3 frame. The helix decoder may need to scan
  // forward to find a valid sync word (0xFF 0xE0 mask), so we may
  // need to retry after refilling the buffer.
  int bytesLeft = _readBufLen - _readBufPos;
  uint8_t* inBuf = _mp3ReadBuf + _readBufPos;

  int result = MP3Decode(_helixDecoder, &inBuf, &bytesLeft, _pcmOutBuf, 0);

  if (result != ERR_MP3_NONE) {
    // ── Decode error handling ──────────────────────────────────────────
    // ERR_MP3_INDATA_UNDERFLOW means the decoder didn't have enough data
    // to find a complete frame. Try refilling the buffer and retrying once.
    if (result == ERR_MP3_INDATA_UNDERFLOW) {
      if (!refillReadBuffer()) {
        Serial.println("[Mp3Player] underflow + EOF");
        return false;
      }
      // Retry with the refilled buffer
      bytesLeft = _readBufLen - _readBufPos;
      inBuf = _mp3ReadBuf + _readBufPos;
      result = MP3Decode(_helixDecoder, &inBuf, &bytesLeft, _pcmOutBuf, 0);

      if (result != ERR_MP3_NONE) {
        // Skip past the bad data and try again next tick
        Serial.printf("[Mp3Player] decode error after refill: %d — skipping\n", result);
        _readBufPos = _readBufLen;  // consume all remaining data
        return true;  // don't stop playback — let the next tick try again
      }
    } else {
      // Other decode errors (bad frame, leftover tag data, corruption, etc.)
      // Scan the REST of the current read buffer for the next valid sync
      // word in one shot, rather than creeping forward 1 byte per tick().
      // A byte-by-byte crawl here is what used to turn a several-KB stretch
      // of non-audio data (ID3v2/APEv2 tags, padding, corrupt frames) into
      // a multi-second stall, since decodeAndWrite() only runs once per
      // tick() and tick() only runs once per main-loop iteration.
      int consumed = (inBuf - (_mp3ReadBuf + _readBufPos));
      int skip = (consumed > 0) ? consumed : 1;

      int searchLen = _readBufLen - (_readBufPos + skip);
      if (searchLen > 0) {
        int syncOffset = MP3FindSyncWord(_mp3ReadBuf + _readBufPos + skip, searchLen);
        if (syncOffset >= 0) {
          skip += syncOffset;
        } else {
          // No sync word anywhere in what's left of the buffer — discard
          // all of it; refillReadBuffer() brings in fresh bytes next call.
          skip = _readBufLen - _readBufPos;
        }
      }

      _readBufPos += skip;
      Serial.printf("[Mp3Player] decode error %d — skipped %d bytes to next sync\n",
                    result, skip);
      return true;  // don't stop — try again next tick
    }
  }

  // ── Decode succeeded — update read buffer position ──────────────────
  int consumed = (inBuf - (_mp3ReadBuf + _readBufPos));
  _readBufPos += consumed;

  // ── Get frame info and check for sample rate changes ────────────────
  MP3FrameInfo frameInfo;
  MP3GetLastFrameInfo(_helixDecoder, &frameInfo);
  if (frameInfo.bitrate > 0) _bitRate = frameInfo.bitrate;
  _totalSamps += frameInfo.outputSamps;

  // Update audio info (for time display)
  _audioInfo.sampleRate = frameInfo.samprate;
  _audioInfo.channels = frameInfo.nChans;
  _audioInfo.bitDepth = frameInfo.bitsPerSample;

  // Make the I2S peripheral's actual clock match this file's real rate/
  // channel count -- see reconfigureI2S() for why this matters. This is
  // a no-op after the first frame of a typical file (rate/channels don't
  // change mid-stream), so it's cheap to check on every frame.
  if (frameInfo.samprate > 0) {
    reconfigureI2S(frameInfo.samprate, frameInfo.nChans);
  }

  // ── Apply volume to PCM samples ─────────────────────────────────────
  int totalSamples = frameInfo.outputSamps;  // total PCM samples (L+R)
  if (totalSamples > 0) {
    applyVolume(_pcmOutBuf, totalSamples);
  }

  // ── Discard frame if we just seeked ─────────────────────────────────
  // See seek(): the first couple of frames after landing at a new byte
  // offset are decoded (to let the decoder's internal state catch up)
  // but deliberately not written to I2S, since they're the frames most
  // likely to contain the seek click artifact.
  if (_seekFramesToDiscard > 0) {
    _seekFramesToDiscard--;
    return true;
  }

  // ── Write PCM data to I2S ───────────────────────────────────────────
  if (totalSamples > 0 && _i2sTxHandle) {
    size_t bytesToWrite = totalSamples * sizeof(int16_t);
    size_t bytesWritten = 0;

    // i2s_channel_write() may not write all bytes in one call if the
    // DMA buffer is full. We loop until all data is written or an
    // error occurs. However, to keep tick() non-blocking, we use a
    // maximum timeout of 0 (no wait) — if the DMA buffer is full, we
    // return true and let the next tick() call try again.
    //
    // Actually, we use a small timeout (100ms) to avoid busy-waiting
    // but still get the data out promptly. The I2S DMA buffer is
    // typically large enough to absorb one frame's worth of data.
    esp_err_t err = i2s_channel_write(_i2sTxHandle, _pcmOutBuf,
                                       bytesToWrite, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK) {
      Serial.printf("[Mp3Player] i2s_channel_write error: %s\n", esp_err_to_name(err));
      // Don't stop playback on a single write error — the DMA buffer
      // might be temporarily full. Return true and try again next tick.
      return true;
    }
  }

  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PRIVATE — Volume scaling
// ═══════════════════════════════════════════════════════════════════════════════
//  Perceptual (log) taper instead of linear.
//
//  Human loudness perception is roughly logarithmic — it takes ~10x the
//  acoustic power for something to sound "twice as loud". A linear
//  0-100 -> 0.0-1.0 amplitude mapping front-loads almost all of the
//  audible change into the top ~20-30% of the slider: 50% linear
//  amplitude is only about -6dB, which barely registers as "half as
//  loud" to the ear, while 90->100% (a small amplitude change) sounds
//  like a big jump. Mapping the slider onto a fixed dB range instead
//  makes each step feel like a consistent, even change in loudness.
//
//  NOTE: 100% still passes samples through completely unscaled (true
//  full digital scale, same as before) — this only changes the *shape*
//  of the curve between 0 and 100, not the ceiling. If the DAC's max
//  output still sounds quieter than another device's at "100%", that's
//  a difference in the analog output stage (line-level DAC output vs.
//  a phone's dedicated headphone amplifier), not something a software
//  volume curve can change — see the note in mp3_manager conversation.
void Mp3Player::applyVolume(int16_t* samples, int numSamples) {
  if (_volume == 100) return;   // No scaling needed at full volume
  if (_volume == 0) {
    // Mute — zero out the buffer
    memset(samples, 0, numSamples * sizeof(int16_t));
    return;
  }

  // Map 1-99 onto MIN_DB..0dB (0% -> MIN_DB, 100% -> 0dB) and convert
  // to a linear gain multiplier. -40dB is a fairly standard bottom end
  // for a "silent-ish but not an abrupt cliff to true mute" volume floor.
  static const float MIN_DB = -40.0f;
  float fraction = _volume / 100.0f;
  float dB = MIN_DB * (1.0f - fraction);
  float gain = powf(10.0f, dB / 20.0f);

  for (int i = 0; i < numSamples; i++) {
    int32_t scaled = static_cast<int32_t>(samples[i] * gain);
    if (scaled > 32767)  scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    samples[i] = static_cast<int16_t>(scaled);
  }
}
