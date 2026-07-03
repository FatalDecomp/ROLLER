#pragma once
#include <stdbool.h>

bool midi_player_init(void);
void midi_player_shutdown(void);

bool midi_player_load(const void *data, int len, bool loop);
void midi_player_start(void);
void midi_player_stop(void);
void midi_player_set_volume(int vol_0_127);
