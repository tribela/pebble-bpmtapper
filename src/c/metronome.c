#include "metronome.h"
#include <pebble.h>

static AppTimer *s_beat_timer = NULL;
static void (*s_flash_cb)(bool) = NULL;
static int32_t s_bpm = 128;
static bool s_running = false;
static MetMode s_mode = MET_SCREEN_VIBE;
static bool s_flash_state = false;

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
  s_beat_timer = app_timer_register(interval, beat_cb, NULL);
}

void metronome_init(void (*flash_cb)(bool on)) {
  s_flash_cb = flash_cb;
  s_beat_timer = NULL;
  s_running = false;
  s_mode = MET_SCREEN_VIBE;
  s_flash_state = false;
}

void metronome_start(int32_t bpm) {
  if (bpm < 30) bpm = 30;
  if (bpm > 300) bpm = 300;
  metronome_stop();
  s_bpm = bpm;
  s_running = true;
  s_flash_state = false;
  if (has_screen(s_mode) && s_flash_cb) s_flash_cb(false);
  light_enable(true);
  uint32_t interval = 60000 / (uint32_t)s_bpm;
  s_beat_timer = app_timer_register(interval, beat_cb, NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "metronome start %d mode %d interval %u", (int)s_bpm, (int)s_mode, (unsigned)interval);
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
  if (bpm < 30) bpm = 30;
  if (bpm > 300) bpm = 300;
  s_bpm = bpm;
  if (s_running) {
    if (s_beat_timer) { app_timer_cancel(s_beat_timer); s_beat_timer = NULL; }
    uint32_t interval = 60000 / (uint32_t)s_bpm;
    s_beat_timer = app_timer_register(interval, beat_cb, NULL);
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "metronome set bpm %d", (int)s_bpm);
}

bool metronome_is_running(void) { return s_running; }
int32_t metronome_get_bpm(void) { return s_bpm; }
void metronome_set_mode(MetMode mode) { s_mode = mode; }
MetMode metronome_get_mode(void) { return s_mode; }
