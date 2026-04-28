#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_PWM_VALUE = 0,
    FLOW_GLOBAL_VARIABLE_EUC_BATTERY_VALUE = 1,
    FLOW_GLOBAL_VARIABLE_DEVICE_BATTERY_VALUE = 2,
    FLOW_GLOBAL_VARIABLE_SPEED_VALUE = 3
};

// Native global variables

extern const char *get_var_mini_batt();
extern void set_var_mini_batt(const char *value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/