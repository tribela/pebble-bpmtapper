#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  MET_SCREEN_VIBE = 0,
  MET_VIBE = 1,
  MET_SCREEN = 2,
  MET_SOUND = 3,
  MET_SCREEN_SOUND = 4,
  MET_COUNT = 5
} MetMode;

void metronome_init(void (*flash_cb)(bool on));
void metronome_start(int32_t bpm);
void metronome_start_aligned(int32_t bpm, uint32_t anchor_ms);
void metronome_stop(void);
void metronome_set_bpm(int32_t bpm);
void metronome_set_bpm_aligned(int32_t bpm, uint32_t anchor_ms);
bool metronome_is_running(void);
int32_t metronome_get_bpm(void);
void metronome_set_mode(MetMode mode);
MetMode metronome_get_mode(void);
