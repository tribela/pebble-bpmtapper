#include <pebble.h>
#include "metronome.h"
#include "settings_window.h"

#define BPM_MIN 30
#define BPM_MAX 300
#define TAP_MIN_INTERVAL_MS 120
#define TAP_WINDOW_MS 5000
#define TAP_HISTORY 32

static Window *s_window;
static TextLayer *s_bpm_layer; // placeholder "Tap to detect"
static TextLayer *s_bpm_num_layer; // fixed-width numbers
static TextLayer *s_bpm_unit_layer; // "BPM" label
static TextLayer *s_genre_layer;
static TextLayer *s_138_prefix_layer; // "Who's Afraid of" above big 138
static TextLayer *s_time_layer;
static int s_block_y = 0;
static int s_block_h = 0;
static AppTimer *s_time_timer = NULL;
static AppTimer *s_detector_timer = NULL;
static int32_t s_bpm = 0; // 0 = no valid BPM yet

static uint32_t s_tap_times[TAP_HISTORY];
static uint8_t s_tap_count_times = 0;
static uint8_t s_tap_head = 0;
static uint32_t s_last_tap_ms = 0;
static uint32_t s_phase_anchor_ms = 0;
static uint8_t s_tap_count = 0;

static char s_time_buf[16]; // static: text_layer keeps pointer, not copy
static char s_bpm_buf[16];

// ---- genre / easter tables (list-driven) ----
static const struct {
  int16_t bpm;
  const char *label;
} s_easter_map[] = {
  {  60, "One Per Second \xE2\x8C\x9A" }, // ⌚ U+231A WATCH looks like pebble
  { 128, "Club Standard" },
  { 138, "Who's Afraid of 138?!" },
  { 140, "Let's Trance \xE2\x99\xA5" },   // ♥ U+2665
  { 174, "DnB Mode: ON" },
};

static const struct {
  int16_t lo;
  int16_t hi;
  const char *label;
} s_genre_map[] = {
  {  60,  79, "Ballad" },
  {  80,  99, "Hip-Hop" },
  { 100, 115, "Pop" },
  { 126, 129, "House" },
  { 130, 137, "Trance" },
  { 138, 142, "Trance / Dubstep" },
  { 143, 160, "Hardstyle" },
  { 168, 180, "DnB" },
};

static const char *genre_for_bpm(int32_t bpm) {
  for (size_t i = 0; i < sizeof(s_easter_map) / sizeof(s_easter_map[0]); i++) {
    if (bpm == s_easter_map[i].bpm) return s_easter_map[i].label;
  }
  for (size_t i = 0; i < sizeof(s_genre_map) / sizeof(s_genre_map[0]); i++) {
    if (bpm >= s_genre_map[i].lo && bpm <= s_genre_map[i].hi) return s_genre_map[i].label;
  }
  return NULL;
}

static void update_display(void);
static void flash_cb(bool on);

static uint32_t get_now_ms(void) {
  time_t t;
  uint16_t ms;
  time_ms(&t, &ms);
  return (uint32_t)t * 1000 + ms;
}

static void update_time(void) {
  if (!s_time_layer) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  const char *fmt = clock_is_24h_style() ? "%H:%M" : "%I:%M%p";
  strftime(s_time_buf, sizeof(s_time_buf), fmt, t);
  if (!clock_is_24h_style() && s_time_buf[0]=='0') memmove(s_time_buf, s_time_buf+1, strlen(s_time_buf)+1);
  text_layer_set_text(s_time_layer, s_time_buf);
}

static void time_tick(void *ctx) {
  (void)ctx;
  update_time();
  s_time_timer = app_timer_register(60000, time_tick, NULL);
}

static char s_bpm_num_buf[8];

static void update_display(void) {
  if (!s_bpm_layer || !s_bpm_num_layer || !s_bpm_unit_layer) return;
  if (s_bpm >= BPM_MIN && s_bpm <= BPM_MAX) {
    if (s_bpm == 138 && s_138_prefix_layer && s_genre_layer) {
      // special 138: use big 138 as part of "Who's Afraid of 138?!"
      snprintf(s_bpm_num_buf, sizeof(s_bpm_num_buf), "%d", (int)s_bpm);
      text_layer_set_text(s_bpm_num_layer, s_bpm_num_buf);
      text_layer_set_text(s_138_prefix_layer, "Who's Afraid of");
      text_layer_set_text(s_genre_layer, "?!");
      // also keep BPM label hidden, show prefix+number+suffix
      layer_set_hidden(text_layer_get_layer(s_bpm_layer), true);
      layer_set_hidden(text_layer_get_layer(s_bpm_num_layer), false);
      layer_set_hidden(text_layer_get_layer(s_bpm_unit_layer), true);
      layer_set_hidden(text_layer_get_layer(s_138_prefix_layer), false);
      layer_set_hidden(text_layer_get_layer(s_genre_layer), false);
      // genre font already 24_BOLD, prefix is 18_BOLD
      return;
    }
    // normal: hide 138 prefix if present
    if (s_138_prefix_layer) layer_set_hidden(text_layer_get_layer(s_138_prefix_layer), true);
    // stacked centered: numbers top, BPM label below
    snprintf(s_bpm_num_buf, sizeof(s_bpm_num_buf), "%d", (int)s_bpm);
    text_layer_set_text(s_bpm_num_layer, s_bpm_num_buf);
    text_layer_set_text(s_bpm_unit_layer, "BPM");
    layer_set_hidden(text_layer_get_layer(s_bpm_layer), true);
    layer_set_hidden(text_layer_get_layer(s_bpm_num_layer), false);
    layer_set_hidden(text_layer_get_layer(s_bpm_unit_layer), false);
    if (s_genre_layer) {
      const char *g = genre_for_bpm(s_bpm);
      if (g) {
        text_layer_set_text(s_genre_layer, g);
        layer_set_hidden(text_layer_get_layer(s_genre_layer), false);
      } else {
        layer_set_hidden(text_layer_get_layer(s_genre_layer), true);
      }
    }
  } else {
    snprintf(s_bpm_buf, sizeof(s_bpm_buf), "Tap to detect");
    text_layer_set_text(s_bpm_layer, s_bpm_buf);
    layer_set_hidden(text_layer_get_layer(s_bpm_layer), false);
    layer_set_hidden(text_layer_get_layer(s_bpm_num_layer), true);
    layer_set_hidden(text_layer_get_layer(s_bpm_unit_layer), true);
    if (s_genre_layer) layer_set_hidden(text_layer_get_layer(s_genre_layer), true);
    if (s_138_prefix_layer) layer_set_hidden(text_layer_get_layer(s_138_prefix_layer), true);
  }
}

static void flash_cb(bool on) {
  MetMode m = settings_get_met_mode();
  if (m == MET_VIBE || m == MET_SOUND) return; // no screen component
  window_set_background_color(s_window, on ? GColorDarkGray : GColorBlack);
}
static void detector_timeout_cb(void *ctx) {
  (void)ctx;
  s_detector_timer = NULL;
  if (metronome_is_running()) return;
  if (s_bpm < BPM_MIN || s_bpm > BPM_MAX) return;
  if (s_tap_count < 2 || s_tap_count_times < 2) return;
  uint32_t anchor = s_phase_anchor_ms; // must capture before clear
  metronome_start_aligned(s_bpm, anchor);
  s_tap_count = 0;
  s_tap_count_times = 0;
  s_tap_head = 0;
  s_last_tap_ms = 0;
  // keep s_phase_anchor_ms for running phase; will be updated on next bpm change
  update_display();
  APP_LOG(APP_LOG_LEVEL_INFO, "auto metronome %d anchor %u", (int)s_bpm, (unsigned)anchor);
}

// auto-start metronome after idle: 2s after first tap, else 2 beats
static void schedule_detector_timeout(void) {
  if (s_detector_timer) { app_timer_cancel(s_detector_timer); s_detector_timer = NULL; }
  uint32_t delay;
  if (s_tap_count_times < 2) {
    delay = 2000;
  } else {
    if (s_bpm < BPM_MIN || s_bpm > BPM_MAX) return;
    uint32_t interval = 60000 / (uint32_t)s_bpm;
    delay = interval * 2;
    if (delay < 400) delay = 400;
    if (delay > 5000) delay = 5000;
  }
  s_detector_timer = app_timer_register(delay, detector_timeout_cb, NULL);
}

// BPM from 5s sliding window: 60000 * intervals / span
static int32_t compute_bpm_5s(uint32_t now) {
  if (s_tap_count_times < 2) return 0;
  int valid_cnt = 0;
  uint32_t oldest = 0, newest = 0;
  for (int i = 0; i < s_tap_count_times; i++) {
    int idx = (s_tap_head - 1 - i + TAP_HISTORY) % TAP_HISTORY;
    uint32_t t = s_tap_times[idx];
    if (now - t <= TAP_WINDOW_MS) {
      valid_cnt++;
      oldest = t;
      if (i == 0) newest = t;
    } else {
      break;
    }
  }
  if (valid_cnt < 2) return 0;
  uint32_t total = newest - oldest;
  if (total == 0) return 0;
  uint32_t intervals = valid_cnt - 1;
  int32_t bpm = (int32_t)((60000UL * intervals + total/2) / total);
  if (bpm < BPM_MIN) bpm = BPM_MIN;
  if (bpm > BPM_MAX) bpm = BPM_MAX;
  return bpm;
}

static void handle_tap(void) {
  if (metronome_is_running()) { // tap while running stops metronome


    metronome_stop();
    flash_cb(false);
    s_tap_count = 0;
    s_tap_count_times = 0;
    s_tap_head = 0;
    s_last_tap_ms = 0;
    s_phase_anchor_ms = 0;
    if (s_detector_timer) { app_timer_cancel(s_detector_timer); s_detector_timer = NULL; }
  }

  uint32_t now = get_now_ms();
  if (s_last_tap_ms != 0 && now - s_last_tap_ms < TAP_MIN_INTERVAL_MS) return; // debounce

  s_tap_times[s_tap_head] = now;
  s_tap_head = (s_tap_head + 1) % TAP_HISTORY;
  if (s_tap_count_times < TAP_HISTORY) s_tap_count_times++;

  s_last_tap_ms = now;
  s_phase_anchor_ms = now;
  if (s_tap_count == 0) s_tap_count = 1;
  else s_tap_count++;

  if (s_tap_count_times >= 2) {
    int32_t bpm = compute_bpm_5s(now);
    if (bpm >= BPM_MIN) s_bpm = bpm;
  }

  update_display();
  schedule_detector_timeout();
  APP_LOG(APP_LOG_LEVEL_INFO, "tap %d bpm %d cnt5s %d", (int)s_tap_count, (int)s_bpm, (int)s_tap_count_times);
}

static void change_bpm(int delta) {
  if (s_bpm < BPM_MIN) s_bpm = BPM_MIN;
  s_bpm += delta;
  if (s_bpm < BPM_MIN) s_bpm = BPM_MIN;
  if (s_bpm > BPM_MAX) s_bpm = BPM_MAX;
  uint32_t now = get_now_ms();
  s_phase_anchor_ms = now;
  if (metronome_is_running()) {
    metronome_set_bpm_aligned(s_bpm, now);
  } else {
    if (s_tap_count > 0) schedule_detector_timeout();
  }
  update_display();
  APP_LOG(APP_LOG_LEVEL_INFO, "bpm manual %d anchor %u", (int)s_bpm, (unsigned)now);
}


static void accel_handler(AccelData *data, uint32_t num_samples) {
  if (settings_get_input_mode() != INPUT_ACCEL) return;
  for (uint32_t i = 0; i < num_samples; i++) {
    int16_t z = data[i].z;
    if (z > 1300 || z < -1300) { // ~1.3g threshold on Z

      handle_tap();
      break;
    }
  }
}

static void touch_handler(const TouchEvent *event, void *ctx) {
  (void)ctx;
  if (settings_get_input_mode() != INPUT_TOUCH) return;
  if (event->type == TouchEvent_Touchdown) handle_tap();
}

static void apply_input_mode(void) {
  accel_data_service_unsubscribe();
  touch_service_unsubscribe();
  InputMode m = settings_get_input_mode();
  if (m == INPUT_ACCEL) {
    accel_data_service_subscribe(5, accel_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  } else if (m == INPUT_TOUCH) {
    if (touch_service_is_enabled()) {
      touch_service_subscribe(touch_handler, NULL);
      window_set_touch_bridge_disabled(s_window, true);
    }
  } else {
    window_set_touch_bridge_disabled(s_window, false);
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "input mode %d", (int)m);
}


static void select_click(ClickRecognizerRef r, void *ctx) {
  (void)r;(void)ctx;
  if (settings_get_input_mode() == INPUT_BUTTON) handle_tap();
}
static void select_long_handler(ClickRecognizerRef r, void *ctx) {
  (void)r;(void)ctx;
  if (metronome_is_running()) {
    metronome_stop();
    flash_cb(false);
    update_display();
  }
  if (s_detector_timer) { app_timer_cancel(s_detector_timer); s_detector_timer = NULL; }
  settings_window_push();
}
static void up_click(ClickRecognizerRef r, void *ctx) { (void)r;(void)ctx; change_bpm(+1); }
static void down_click(ClickRecognizerRef r, void *ctx) { (void)r;(void)ctx; change_bpm(-1); }
static void back_click(ClickRecognizerRef r, void *ctx) {
  (void)r;(void)ctx;
  if (metronome_is_running()) {
    metronome_stop();
    flash_cb(false);
    s_tap_count = 0;
    s_tap_count_times = 0;
    s_tap_head = 0;
    s_last_tap_ms = 0;
    s_phase_anchor_ms = 0;
    if (s_detector_timer) { app_timer_cancel(s_detector_timer); s_detector_timer = NULL; }
    update_display();
  } else {
    window_stack_pop(true);
  }
}

static void click_config(void *ctx) {
  (void)ctx;
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long_handler, NULL);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}


static void window_appear(Window *w) {
  (void)w;
  light_enable(true);
  settings_load();
  metronome_set_mode(settings_get_met_mode());
  apply_input_mode();
  update_time();
  if (s_time_timer) { app_timer_cancel(s_time_timer); s_time_timer = NULL; }
  s_time_timer = app_timer_register(60000, time_tick, NULL);
  update_display();
}

static void window_disappear(Window *w) {
  (void)w;
  if (s_time_timer) { app_timer_cancel(s_time_timer); s_time_timer = NULL; }
  accel_data_service_unsubscribe();
  touch_service_unsubscribe();
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_time_layer = text_layer_create(GRect(0, 0, b.size.w, 30));
  text_layer_set_background_color(s_time_layer, GColorWhite);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  // placeholder centered (shown when no BPM)
  s_bpm_layer = text_layer_create(GRect(0, (b.size.h - 50)/2, b.size.w, 50));
  text_layer_set_background_color(s_bpm_layer, GColorClear);
  text_layer_set_text_color(s_bpm_layer, GColorWhite);
  text_layer_set_text_alignment(s_bpm_layer, GTextAlignmentCenter);
  text_layer_set_font(s_bpm_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(root, text_layer_get_layer(s_bpm_layer));

  // stacked: numbers large top, BPM small below
  int block_h = 50 + 2 + 18;
  int block_y = (b.size.h - block_h)/2;
  if (block_y < 30) block_y = 30; // keep below time bar
  s_block_y = block_y;
  s_block_h = block_h;
  s_bpm_num_layer = text_layer_create(GRect(0, block_y, b.size.w, 50));
  text_layer_set_background_color(s_bpm_num_layer, GColorClear);
  text_layer_set_text_color(s_bpm_num_layer, GColorWhite);
  text_layer_set_text_alignment(s_bpm_num_layer, GTextAlignmentCenter);
  text_layer_set_font(s_bpm_num_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  layer_add_child(root, text_layer_get_layer(s_bpm_num_layer));

  s_bpm_unit_layer = text_layer_create(GRect(0, block_y + 50 + 2, b.size.w, 18));
  text_layer_set_background_color(s_bpm_unit_layer, GColorClear);
  text_layer_set_text_color(s_bpm_unit_layer, GColorWhite);
  text_layer_set_text_alignment(s_bpm_unit_layer, GTextAlignmentCenter);
  text_layer_set_font(s_bpm_unit_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_bpm_unit_layer));
  layer_set_hidden(text_layer_get_layer(s_bpm_num_layer), true);
  layer_set_hidden(text_layer_get_layer(s_bpm_unit_layer), true);

  // genre / easter label below BPM unit (word-wrap for long easter, large font)
  s_genre_layer = text_layer_create(GRect(0, block_y + block_h + 4, b.size.w, 55));
  text_layer_set_background_color(s_genre_layer, GColorClear);
  text_layer_set_text_color(s_genre_layer, GColorWhite);
  text_layer_set_text_alignment(s_genre_layer, GTextAlignmentCenter);
  text_layer_set_font(s_genre_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_genre_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_genre_layer));
  layer_set_hidden(text_layer_get_layer(s_genre_layer), true);

  // 138 special: "Who's Afraid of" above the big 138
  int prefix_h = 22;
  int prefix_y = block_y - prefix_h - 2;
  if (prefix_y < 32) prefix_y = 32; // keep below time bar (30)
  s_138_prefix_layer = text_layer_create(GRect(0, prefix_y, b.size.w, prefix_h));
  text_layer_set_background_color(s_138_prefix_layer, GColorClear);
  text_layer_set_text_color(s_138_prefix_layer, GColorWhite);
  text_layer_set_text_alignment(s_138_prefix_layer, GTextAlignmentCenter);
  text_layer_set_font(s_138_prefix_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_138_prefix_layer));
  layer_set_hidden(text_layer_get_layer(s_138_prefix_layer), true);

  update_time();
  update_display();
}

static void window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_138_prefix_layer); s_138_prefix_layer = NULL;
  text_layer_destroy(s_genre_layer); s_genre_layer = NULL;
  text_layer_destroy(s_bpm_unit_layer); s_bpm_unit_layer = NULL;
  text_layer_destroy(s_bpm_num_layer); s_bpm_num_layer = NULL;
  text_layer_destroy(s_bpm_layer); s_bpm_layer = NULL;
  text_layer_destroy(s_time_layer); s_time_layer = NULL;
}

static void init(void) {
  s_bpm = 0;
  s_tap_count_times = 0;
  s_tap_head = 0;
  s_tap_count = 0;
  s_last_tap_ms = 0;
  s_phase_anchor_ms = 0;
  settings_load();
  metronome_init(flash_cb);
  metronome_set_mode(settings_get_met_mode());
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
    .appear = window_appear, .disappear = window_disappear
  });
  window_stack_push(s_window, true);
  APP_LOG(APP_LOG_LEVEL_INFO, "init bpm %d input %d met %d", (int)s_bpm, (int)settings_get_input_mode(), (int)settings_get_met_mode());
}

static void deinit(void) {
  metronome_stop();
  if (s_time_timer) { app_timer_cancel(s_time_timer); s_time_timer = NULL; }
  if (s_detector_timer) { app_timer_cancel(s_detector_timer); s_detector_timer = NULL; }
  accel_data_service_unsubscribe();
  touch_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
