// Saving and loading worlds and settings on the SD card
#pragma once
#include "common.h"
#include <stdio.h>

bool save_init(void);                 // mount FAT; returns availability
void save_scan_slots(void);           // fill slot_info
bool save_world(int slot, void (*progress)(int pct));
bool load_world(int slot, void (*progress)(int pct));
bool delete_world(int slot);
void save_settings(void);
void load_settings(void);
bool save_world_fp(FILE *f, void (*progress)(int pct));
bool load_world_fp(FILE *f, void (*progress)(int pct));
