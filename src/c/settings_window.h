#pragma once
#include <stdint.h>
#include "metronome.h"

#define PERSIST_KEY_INPUT_MODE 10
#define PERSIST_KEY_MET_MODE   11

typedef enum { INPUT_BUTTON = 0, INPUT_TOUCH = 1, INPUT_ACCEL = 2 } InputMode;

void settings_window_push(void);
InputMode settings_get_input_mode(void);
MetMode settings_get_met_mode(void);
void settings_load(void);
