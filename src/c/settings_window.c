#include <pebble.h>
#include "settings_window.h"
#include "metronome.h"

static Window *s_window;
static MenuLayer *s_menu_layer;

static InputMode s_input = INPUT_BUTTON;
static MetMode s_met = MET_BOTH;

static const char* input_label(InputMode m) {
  switch(m){case INPUT_BUTTON:return "Button";case INPUT_TOUCH:return "Touch";case INPUT_ACCEL:return "Motion(Z)";default:return "?";}
}
static const char* met_label(MetMode m) {
  switch(m){case MET_BOTH:return "Both";case MET_VIBE:return "Vibe";case MET_SCREEN:return "Screen";default:return "?";}
}

InputMode settings_get_input_mode(void){ return s_input; }
MetMode settings_get_met_mode(void){ return s_met; }

void settings_load(void){
  if(persist_exists(PERSIST_KEY_INPUT_MODE)) s_input = (InputMode)persist_read_int(PERSIST_KEY_INPUT_MODE);
  else s_input = INPUT_BUTTON;
  if(persist_exists(PERSIST_KEY_MET_MODE)) s_met = (MetMode)persist_read_int(PERSIST_KEY_MET_MODE);
  else s_met = MET_BOTH;
  if(s_input>INPUT_ACCEL) s_input=INPUT_BUTTON;
  if(s_met>MET_SCREEN) s_met=MET_BOTH;
  metronome_set_mode(s_met);
}

static void persist_save(void){
  persist_write_int(PERSIST_KEY_INPUT_MODE, (int)s_input);
  persist_write_int(PERSIST_KEY_MET_MODE, (int)s_met);
  metronome_set_mode(s_met);
}

// MenuLayer callbacks
static uint16_t get_num_sections(MenuLayer *ml, void *ctx){ (void)ml;(void)ctx; return 1; }
static uint16_t get_num_rows(MenuLayer *ml, uint16_t sec, void *ctx){ (void)ml;(void)sec;(void)ctx; return 2; }
static int16_t get_cell_height(MenuLayer *ml, MenuIndex *cell, void *ctx){ (void)ml;(void)cell;(void)ctx; return 44; }
static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *ctx2){
  (void)ctx2;
  if(cell_index->row==0){
    menu_cell_basic_draw(ctx, cell_layer, "Input Mode", input_label(s_input), NULL);
  } else {
    menu_cell_basic_draw(ctx, cell_layer, "Metronome", met_label(s_met), NULL);
  }
}
static void select_click(MenuLayer *ml, MenuIndex *cell_index, void *ctx){
  (void)ml;(void)ctx;
  if(cell_index->row==0){
    s_input = (InputMode)((s_input+1)%3);
  } else {
    s_met = (MetMode)((s_met+1)%3);
  }
  persist_save();
  menu_layer_reload_data(ml);
}

static void window_load(Window *window){
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window){
  (void)window;
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  window_destroy(window);
  s_window = NULL;
}

void settings_window_push(void){
  metronome_stop();
  settings_load();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){.load=window_load,.unload=window_unload});
  window_stack_push(s_window, true);
}
