/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/rgb_fx_control_group.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_rgb_fx_control_group

#define DEVICE_ADDR(idx) DEVICE_DT_GET(DT_INST(idx, zmk_rgb_fx_control_group)),

/**
 * Control animation instance pointers.
 */
static const struct device *control_fx[] = {DT_INST_FOREACH_STATUS_OKAY(DEVICE_ADDR)};

/**
 * Size of control animation instance pointers array.
 */
static const uint8_t control_fx_size = sizeof(control_fx);

/**
 * Index of the current default control animation instance.
 */
static uint8_t current_zone = 0;

#define DT_DRV_COMPAT zmk_behavior_rgb_fx

static int
on_keymap_binding_convert_central_state_dependent_params(struct zmk_behavior_binding *binding,
                                                         struct zmk_behavior_binding_event event) {
    if ((binding->param1 >> 24) == 0xff) {
        binding->param1 = (current_zone << 24) | (binding->param1 & 0x00ffffff);
    }

    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint8_t value = binding->param1 & 0xff;
    uint8_t command = (binding->param1 >> 16) & 0xff;
    uint8_t zone = (binding->param1 >> 24) & 0xff;

    LOG_INF("rgbfx pressed: param=0x%08x cmd=%d zone=%d value=%d groups=%d", binding->param1,
            command, zone, value, (int)control_fx_size);

    if (command == RGB_FX_CMD_NEXT_CONTROL_ZONE) {
        current_zone++;

        if (current_zone == control_fx_size) {
            current_zone = 0;
        }
        return 0;
    }

    if (command == RGB_FX_CMD_PREVIOUS_CONTROL_ZONE) {
        if (current_zone == 0) {
            current_zone = control_fx_size;
        }

        current_zone--;
        return 0;
    }

    if (control_fx_size <= zone) {
        return -ENOTSUP;
    }

    return zmk_rgb_fx_control_handle_command(control_fx[zone], command, value);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_rgb_fx_init(const struct device *dev) { return 0; }

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/*
 * Studio validates every binding against the behavior's parameter metadata, and
 * a behavior without any rejects everything but 0/0: binding an RGB key from
 * Studio failed with "invalid parameters". param1 packs zone << 24 | command << 16
 * | value (dt-bindings/zmk/rgb_fx.h), so the usual zone-0 commands are listed by
 * name (zmk.studio shows them in its picker) and two ranges accept any other
 * well-formed command.
 */
#define RGBFX_VAL(name, cmd)                                                                       \
    {                                                                                              \
        .display_name = name,                                                                      \
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,                                               \
        .value = ((uint32_t)(cmd) << 16),                                                          \
    }

static const struct behavior_parameter_value_metadata param1_values[] = {
    RGBFX_VAL("Toggle", RGB_FX_CMD_TOGGLE),
    RGBFX_VAL("Next effect", RGB_FX_CMD_NEXT),
    RGBFX_VAL("Previous effect", RGB_FX_CMD_PREVIOUS),
    RGBFX_VAL("Brighter", RGB_FX_CMD_BRIGHTEN),
    RGBFX_VAL("Dimmer", RGB_FX_CMD_DIM),
    RGBFX_VAL("Hue up", RGB_FX_CMD_HUE_UP),
    RGBFX_VAL("Hue down", RGB_FX_CMD_HUE_DOWN),
    RGBFX_VAL("Faster", RGB_FX_CMD_SPEED_UP),
    RGBFX_VAL("Slower", RGB_FX_CMD_SPEED_DOWN),
    RGBFX_VAL("Next zone", RGB_FX_CMD_NEXT_CONTROL_ZONE),
    RGBFX_VAL("Previous zone", RGB_FX_CMD_PREVIOUS_CONTROL_ZONE),
    {
        /* any command with a value (SELECT effect n) on zones 0..15 */
        .display_name = "Command (zone 0-15)",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = 0, .max = 0x0FFFFFFF},
    },
    {
        /* zone 0xff = "the current zone"; the matcher compares param (uint32_t)
         * against these int32_t bounds, so -16777216..-1 is 0xff000000..0xffffffff */
        .display_name = "Command (current zone)",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = (int32_t)0xFF000000, .max = -1},
    },
};

static const struct behavior_parameter_metadata_set param_set = {
    .param1_values = param1_values,
    .param1_values_len = ARRAY_SIZE(param1_values),
};

static const struct behavior_parameter_metadata_set sets[] = {param_set};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(sets),
    .sets = sets,
};

#endif /* IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA) */

static const struct behavior_driver_api behavior_rgb_fx_driver_api = {
    /* Central-only ON PURPOSE (default locality). This behavior used to be
     * GLOBAL so both halves would apply each command, but relative commands
     * (toggle = flip, next = +1) applied to states that can legitimately
     * diverge (per-half USB power) invert or desync the halves. Now the
     * central is the single source of truth: it handles the command and
     * pushes the resulting ABSOLUTE state to the peripheral via `rgbsync`
     * (see control_group.c). */
    .binding_convert_central_state_dependent_params =
        on_keymap_binding_convert_central_state_dependent_params,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_rgb_fx_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_rgb_fx_driver_api);
