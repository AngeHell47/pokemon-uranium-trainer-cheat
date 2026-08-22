#pragma once

#include <windows.h>

struct TrainerProfile {
    char name[32];
    int gender;          // 0=garcon, 1=fille, 2=neutre (ID de joueur Uranium)
    int play_seconds;
    int badge_mask;      // bits 0..7
};

bool opt_trainer_manager_init(const char* ini_path);
void opt_trainer_manager_start();
void opt_trainer_manager_stop();
void opt_trainer_manager_shutdown();
void opt_trainer_manager_refresh();
bool opt_trainer_manager_copy_profile(TrainerProfile* out, LONG* revision,
                                      char* status, int status_capacity);
void opt_trainer_manager_apply(const TrainerProfile& profile);
