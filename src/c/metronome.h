#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { MET_BOTH = 0, MET_VIBE = 1, MET_SCREEN = 2 } MetMode;

void metronome_init(void (*flash_cb)(bool on));
void metronome_start(int32_t bpm);
void metronome_stop(void);
void metronome_set_bpm(int32_t bpm);
bool metronome_is_running(void);
int32_t metronome_get_bpm(void);
void metronome_set_mode(MetMode mode);
MetMode metronome_get_mode(void);
