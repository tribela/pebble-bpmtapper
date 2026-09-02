#include "metronome.h"
#include <pebble.h>

static AppTimer *s_beat_timer = NULL;
static void (*s_flash_cb)(bool) = NULL;
static int32_t s_bpm = 128;
static bool s_running = false;
static MetMode s_mode = MET_SCREEN_VIBE;
static bool s_flash_state = false;
static uint32_t s_anchor_ms = 0;
static uint32_t s_beat_n = 0;

static const uint32_t s_vibe_segments[] = { 20 };
static const VibePattern s_vibe = { .durations = s_vibe_segments, .num_segments = 1 };

static inline bool has_screen(MetMode m) {
  return m == MET_SCREEN || m == MET_SCREEN_VIBE || m == MET_SCREEN_SOUND;
}
static inline bool has_vibe(MetMode m) {
  return m == MET_VIBE || m == MET_SCREEN_VIBE;
}
static inline bool has_sound(MetMode m) {
  return m == MET_SOUND || m == MET_SCREEN_SOUND;
}

static uint32_t get_now_ms_local(void) {
  time_t t; uint16_t ms; time_ms(&t, &ms);
  return (uint32_t)t * 1000 + ms;
}

static void beat_cb(void *ctx) {
  (void)ctx;
  if (!s_running) return;
  s_beat_timer = NULL;

  if (has_screen(s_mode) && s_flash_cb) {
    s_flash_state = !s_flash_state; // alternate screen each beat
    s_flash_cb(s_flash_state);
  }

  if (has_vibe(s_mode)) vibes_enqueue_custom_pattern(s_vibe);
  if (has_sound(s_mode)) speaker_play_tone(1200, 25, 35, SpeakerWaveformSquare);

  if (s_bpm <= 0) return;
  uint32_t interval = 60000 / (uint32_t)s_bpm;
  s_beat_n++;
  uint64_t target = (uint64_t)s_anchor_ms + (uint64_t)(s_beat_n + 1) * 60000 / (uint32_t)s_bpm;
  uint32_t now = get_now_ms_local();
  int32_t delay = (int32_t)(target - now);
  // keep near-zero guard: avoid scheduling in the past or too close
  if (delay < 5) delay += (int32_t)interval;
  if (delay < 15) delay += (int32_t)interval;
  if (delay < 5) delay = 5;
  s_beat_timer = app_timer_register((uint32_t)delay, beat_cb, NULL);
}

void metronome_init(void (*flash_cb)(bool on)) {
  s_flash_cb = flash_cb;
  s_beat_timer = NULL;
  s_running = false;
  s_mode = MET_SCREEN_VIBE;
  s_flash_state = false;
  s_anchor_ms = 0;
  s_beat_n = 0;
}

void metronome_start(int32_t bpm) {
  metronome_start_aligned(bpm, 0);
}

void metronome_start_aligned(int32_t bpm, uint32_t anchor_ms) {
  if (bpm < 30) bpm = 30;
  if (bpm > 300) bpm = 300;
  metronome_stop();
  s_bpm = bpm;
  s_running = true;
  s_flash_state = false;
  if (has_screen(s_mode) && s_flash_cb) s_flash_cb(false);
  light_enable(true);
  uint32_t interval = 60000 / (uint32_t)s_bpm;
  uint32_t delay;
  uint32_t now = get_now_ms_local();
  if (anchor_ms == 0) {
    s_anchor_ms = now;
    s_beat_n = 0;
    delay = interval;
  } else {
    s_anchor_ms = anchor_ms;
    uint32_t elapsed = now - anchor_ms;
    // beat index of next beat (0-based: anchor is beat 0)
    s_beat_n = (uint32_t)((uint64_t)elapsed * (uint32_t)s_bpm / 60000);
    uint64_t target = (uint64_t)s_anchor_ms + (uint64_t)(s_beat_n + 1) * 60000 / (uint32_t)s_bpm;
    int32_t d = (int32_t)(target - now);
    if (d == 0) d = 5; // exactly on grid -> fire immediately
    else if (d < 15) d += (int32_t)interval;
    if (d < 5) d = 5;
    delay = (uint32_t)d;
  }
  s_beat_timer = app_timer_register(delay, beat_cb, NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "metronome start %d mode %d interval %u delay %u anchor %u beat %u", (int)s_bpm, (int)s_mode, (unsigned)interval, (unsigned)delay, (unsigned)anchor_ms, (unsigned)s_beat_n);
}

void metronome_stop(void) {
  if (s_beat_timer) { app_timer_cancel(s_beat_timer); s_beat_timer = NULL; }
  s_running = false;
  s_flash_state = false;
  if (s_flash_cb) s_flash_cb(false);
  speaker_stop();
  light_set_system_color();
  light_enable(false);
  APP_LOG(APP_LOG_LEVEL_INFO, "metronome stop");
}

void metronome_set_bpm(int32_t bpm) {
  metronome_set_bpm_aligned(bpm, 0);
}

void metronome_set_bpm_aligned(int32_t bpm, uint32_t anchor_ms) {
  if (bpm < 30) bpm = 30;
  if (bpm > 300) bpm = 300;
  s_bpm = bpm;
  if (s_running) {
    if (s_beat_timer) { app_timer_cancel(s_beat_timer); s_beat_timer = NULL; }
    uint32_t interval = 60000 / (uint32_t)s_bpm;
    uint32_t now = get_now_ms_local();
    if (anchor_ms == 0) {
      // no anchor: keep existing anchor, recompute phase with new BPM
      uint32_t elapsed = now - s_anchor_ms;
      s_beat_n = (uint32_t)((uint64_t)elapsed * (uint32_t)s_bpm / 60000);
      uint64_t target = (uint64_t)s_anchor_ms + (uint64_t)(s_beat_n + 1) * 60000 / (uint32_t)s_bpm;
      int32_t d = (int32_t)(target - now);
      if (d < 15) d += (int32_t)interval;
      if (d < 5) d = 5;
      s_beat_timer = app_timer_register((uint32_t)d, beat_cb, NULL);
      APP_LOG(APP_LOG_LEVEL_INFO, "metronome set bpm %d delay %u anchor %u beat %u", (int)s_bpm, (unsigned)d, (unsigned)s_anchor_ms, (unsigned)s_beat_n);
    } else {
      // silent phase reset: anchor is the new beat 0, next beat is +1 interval
      s_anchor_ms = anchor_ms;
      s_beat_n = 0;
      uint64_t target = (uint64_t)s_anchor_ms + (uint64_t)60000 / (uint32_t)s_bpm;
      int32_t d = (int32_t)(target - now);
      if (d < 15) d += (int32_t)interval;
      if (d < 5) d = 5;
      s_beat_timer = app_timer_register((uint32_t)d, beat_cb, NULL);
      APP_LOG(APP_LOG_LEVEL_INFO, "metronome set bpm %d delay %u anchor %u (phase reset, silent)", (int)s_bpm, (unsigned)d, (unsigned)anchor_ms);
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "metronome set bpm %d (stopped)", (int)s_bpm);
  }
}

bool metronome_is_running(void) { return s_running; }
int32_t metronome_get_bpm(void) { return s_bpm; }
void metronome_set_mode(MetMode mode) { s_mode = mode; }
MetMode metronome_get_mode(void) { return s_mode; }
