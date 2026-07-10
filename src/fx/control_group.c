/*
 * Copyright (c) 2024 Kuba Birecki
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_rgb_fx_control_group

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/rgb_fx.h>

#include <zmk/rgb_fx.h>
#include <zmk/rgb_fx_control_group.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Each half reacts to ITS OWN USB port: RGB defaults to OFF on battery
 * and ON at 100% while USB power is present (see the usb listener at the
 * bottom). A manual toggle while on battery comes up at 10%. */
static bool fx_control_group_usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

/* Brightness curve: the lowest step is 10% (battery default, barely sips
 * power); the remaining steps spread evenly up to 100%. With the default
 * 4 usable steps: 10 / 40 / 70 / 100%. */
static float fx_control_group_brightness_scale(uint8_t brightness, uint8_t steps) {
    if (brightness >= steps) {
        return 1.0f;
    }

    return 0.1f + (0.9f * (float)(brightness - 1) / (float)(steps - 1));
}

#define PHANDLE_TO_DEVICE(node_id, prop, idx) DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

struct fx_control_group_work_context {
    const struct device *control_group;
    struct k_work_delayable save_work;
};

struct fx_control_group_config {
    const struct device **fx;
    const size_t fx_size;
    const uint8_t brightness_steps;
    struct fx_control_group_work_context *work;
    struct settings_handler *settings_handler;
};

struct fx_control_group_data {
    bool active;
    uint8_t brightness;
    size_t current_fx_idx;
    uint16_t hue_offset;
    uint8_t speed_step;
};

static int fx_control_group_load_settings(const struct device *dev, const char *name, size_t len,
                                          settings_read_cb read_cb, void *cb_arg) {
#if IS_ENABLED(CONFIG_SETTINGS)
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(struct fx_control_group_data)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, dev->data, sizeof(struct fx_control_group_data));
        if (rc >= 0) {
            zmk_rgb_fx_hue_offset = ((struct fx_control_group_data *)dev->data)->hue_offset % 360;
            zmk_rgb_fx_speed_set(((struct fx_control_group_data *)dev->data)->speed_step);
            /* Sanitize saved state: a persisted brightness of 0 leaves the RGB
             * black forever (render x0) even if the toggle is pressed. */
            const struct fx_control_group_config *config = dev->config;
            struct fx_control_group_data *data = dev->data;

            if (data->brightness == 0) {
                data->brightness = config->brightness_steps;
            }
            if (data->current_fx_idx >= config->fx_size) {
                data->current_fx_idx = 0;
            }
            /* Never restore the saved on/off state: the power source
             * decides at boot. The USB listener turns the RGB on when
             * the cable is (or gets) plugged; on battery it stays off
             * until the user toggles it. */
            data->active = false;
            return 0;
        }

        return rc;
    }

    return -ENOENT;
#else
    return 0;
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void fx_control_group_save_work(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct fx_control_group_work_context *ctx = CONTAINER_OF(dwork, struct fx_control_group_work_context, save_work);

    const struct device *dev = ctx->control_group;

    char path[40];
    snprintf(path, 40, "%s/state", dev->name);

    settings_save_one(path, dev->data, sizeof(struct fx_control_group_data));
};

static int fx_control_group_save_settings(const struct device *dev) {
    const struct fx_control_group_config *config = dev->config;
    struct fx_control_group_work_context *ctx = config->work;

    k_work_cancel_delayable(&ctx->save_work);

    return k_work_reschedule(&ctx->save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

int zmk_rgb_fx_control_handle_command(const struct device *dev, uint8_t command, uint8_t param) {
    const struct fx_control_group_config *config = dev->config;
    struct fx_control_group_data *data = dev->data;

    LOG_INF("fx ctrl: cmd=%d param=%d active=%d idx=%d brt=%d/%d", command, param, (int)data->active,
            (int)data->current_fx_idx, (int)data->brightness, (int)config->brightness_steps);

    switch (command) {
    case RGB_FX_CMD_TOGGLE:
        data->active = !data->active;

        if (data->active) {
            /* Manual power-on default: 100% on USB power, 10% on battery
             * (the encoder can raise it afterwards). */
            data->brightness = fx_control_group_usb_powered() ? config->brightness_steps : 1;
            rgb_fx_start(config->fx[data->current_fx_idx]);
            break;
        }

        rgb_fx_stop(config->fx[data->current_fx_idx]);
        break;
    /* Upstream didn't stop the outgoing effect nor start the incoming one on
     * change (NEXT/PREVIOUS/SELECT): the new effect never got its
     * on_start and stayed black until reboot. */
    case RGB_FX_CMD_NEXT:
        if (data->active) {
            rgb_fx_stop(config->fx[data->current_fx_idx]);
        }

        data->current_fx_idx++;

        if (data->current_fx_idx == config->fx_size) {
            data->current_fx_idx = 0;
        }

        if (data->active) {
            rgb_fx_start(config->fx[data->current_fx_idx]);
        }
        break;
    case RGB_FX_CMD_PREVIOUS:
        if (data->active) {
            rgb_fx_stop(config->fx[data->current_fx_idx]);
        }

        if (data->current_fx_idx == 0) {
            data->current_fx_idx = config->fx_size;
        }

        data->current_fx_idx--;

        if (data->active) {
            rgb_fx_start(config->fx[data->current_fx_idx]);
        }
        break;
    case RGB_FX_CMD_SELECT:
        if (config->fx_size <= param) {
            return -ENOTSUP;
        }

        if (data->active) {
            rgb_fx_stop(config->fx[data->current_fx_idx]);
        }

        data->current_fx_idx = param;

        if (data->active) {
            rgb_fx_start(config->fx[data->current_fx_idx]);
        }
        break;
    case RGB_FX_CMD_HUE_UP:
        zmk_rgb_fx_hue_offset = (zmk_rgb_fx_hue_offset + 20) % 360;
        data->hue_offset = zmk_rgb_fx_hue_offset;
        break;
    case RGB_FX_CMD_HUE_DOWN:
        zmk_rgb_fx_hue_offset = (zmk_rgb_fx_hue_offset + 340) % 360;
        data->hue_offset = zmk_rgb_fx_hue_offset;
        break;
    case RGB_FX_CMD_SPEED_UP:
        if (zmk_rgb_fx_speed_get() < 4) {
            zmk_rgb_fx_speed_set(zmk_rgb_fx_speed_get() + 1);
        }
        data->speed_step = zmk_rgb_fx_speed_get();
        break;
    case RGB_FX_CMD_SPEED_DOWN:
        if (zmk_rgb_fx_speed_get() > 0) {
            zmk_rgb_fx_speed_set(zmk_rgb_fx_speed_get() - 1);
        }
        data->speed_step = zmk_rgb_fx_speed_get();
        break;
    case RGB_FX_CMD_DIM:
        /* Minimum 1: turning off is the TOGGLE's job. A persisted brightness 0
         * left the keyboard black forever across reflashes. */
        if (data->brightness <= 1) {
            return 0;
        }

        data->brightness--;
        break;
    case RGB_FX_CMD_BRIGHTEN:
        if (data->brightness == config->brightness_steps) {
            return 0;
        }

        if (data->brightness == 0) {
            rgb_fx_start(config->fx[data->current_fx_idx]);
        }

        data->brightness++;
        break;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    fx_control_group_save_settings(dev);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

    // Force refresh
    zmk_rgb_fx_request_frames(1);

    return 0;
}

static void fx_control_group_render_frame(const struct device *dev, struct rgb_fx_pixel *pixels,
                                          size_t num_pixels) {
    const struct fx_control_group_config *config = dev->config;
    const struct fx_control_group_data *data = dev->data;

    if (!data->active) {
        return;
    }

    rgb_fx_render_frame(config->fx[data->current_fx_idx], pixels, num_pixels);

    if (data->brightness == config->brightness_steps) {
        return;
    }

    float brightness =
        fx_control_group_brightness_scale(data->brightness, config->brightness_steps);

    for (size_t i = 0; i < num_pixels; ++i) {
        pixels[i].value.r *= brightness;
        pixels[i].value.g *= brightness;
        pixels[i].value.b *= brightness;
    }
}

static void fx_control_group_start(const struct device *dev) {
    const struct fx_control_group_config *config = dev->config;
    const struct fx_control_group_data *data = dev->data;

    if (!data->active) {
        return;
    }

    rgb_fx_start(config->fx[data->current_fx_idx]);
}

static void fx_control_group_stop(const struct device *dev) {
    const struct fx_control_group_config *config = dev->config;
    const struct fx_control_group_data *data = dev->data;

    rgb_fx_stop(config->fx[data->current_fx_idx]);
}

static int fx_control_group_init(const struct device *dev) {
#if IS_ENABLED(CONFIG_SETTINGS)
    const struct fx_control_group_config *config = dev->config;

    settings_subsys_init();

    settings_register(config->settings_handler);

    k_work_init_delayable(&config->work->save_work, fx_control_group_save_work);

    settings_load_subtree(dev->name);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

    return 0;
}

static const struct rgb_fx_api fx_control_group_api = {
    .on_start = fx_control_group_start,
    .on_stop = fx_control_group_stop,
    .render_frame = fx_control_group_render_frame,
};

#define FX_CONTROL_GROUP_DEVICE(idx)                                                               \
                                                                                                   \
    static const struct device *fx_control_group_##idx##_fx[] = {                                  \
        DT_INST_FOREACH_PROP_ELEM(idx, fx, PHANDLE_TO_DEVICE)};                                    \
                                                                                                   \
    static struct fx_control_group_work_context fx_control_group_##idx##_work = {                  \
        .control_group = DEVICE_DT_GET(DT_DRV_INST(idx)),                                          \
    };                                                                                             \
                                                                                                   \
    static int fx_control_group_##idx##_load_settings(const char *name, size_t len,                \
                                                      settings_read_cb read_cb, void *cb_arg) {    \
        const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(idx));                                \
                                                                                                   \
        return fx_control_group_load_settings(dev, name, len, read_cb, cb_arg);                    \
    }                                                                                              \
                                                                                                   \
    static struct settings_handler fx_control_group_##idx##_settings_handler = {                   \
        .name = "fx_control_group_"#idx,                                                           \
        .h_set = fx_control_group_##idx##_load_settings,                                           \
    };                                                                                             \
                                                                                                   \
    static const struct fx_control_group_config fx_control_group_##idx##_config = {                \
        .fx = fx_control_group_##idx##_fx,                                                         \
        .fx_size = DT_INST_PROP_LEN(idx, fx),                                                      \
        .brightness_steps = DT_INST_PROP(idx, brightness_steps) - 1,                               \
        .work = &fx_control_group_##idx##_work,                                                    \
        .settings_handler = &fx_control_group_##idx##_settings_handler,                            \
    };                                                                                             \
                                                                                                   \
    static struct fx_control_group_data fx_control_group_##idx##_data = {                          \
        /* OFF by default: the USB listener turns it on (at 100%) when     \
         * cable power is present; on battery the user toggles it on at    \
         * the 10% step. Also keeps 36 LEDs from slamming the rail at boot. */ \
        .active = false,                                                                           \
        .brightness = 1,                                                                           \
        .current_fx_idx = 0,                                                                       \
        .speed_step = 2, /* 1x */                                                                  \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(idx, &fx_control_group_init, NULL, &fx_control_group_##idx##_data,       \
                          &fx_control_group_##idx##_config, POST_KERNEL,                           \
                          CONFIG_APPLICATION_INIT_PRIORITY, &fx_control_group_api);

DT_INST_FOREACH_STATUS_OKAY(FX_CONTROL_GROUP_DEVICE);

/* ---- USB power listener: RGB follows the cable ----
 *
 * Runs independently on each half (each one watches its own USB port):
 *   - cable plugged in  -> RGB ON at 100%
 *   - cable pulled out  -> RGB OFF
 * This also covers boot: the USB connection event fires during startup,
 * so a half that boots with the cable in comes up lit, and a half that
 * boots on battery stays dark (data->active defaults to false). */
#if IS_ENABLED(CONFIG_ZMK_USB) && DT_HAS_CHOSEN(zmk_rgb_fx)

static int fx_control_group_usb_listener(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh) == NULL) {
        return -ENOTSUP;
    }

    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zmk_rgb_fx));
    const struct fx_control_group_config *config = dev->config;
    struct fx_control_group_data *data = dev->data;

    if (zmk_usb_is_powered()) {
        data->brightness = config->brightness_steps; /* 100% on cable power */

        if (!data->active) {
            data->active = true;
            rgb_fx_start(config->fx[data->current_fx_idx]);
        }
    } else if (data->active) {
        data->active = false;
        rgb_fx_stop(config->fx[data->current_fx_idx]);
    }

    zmk_rgb_fx_request_frames(1);

    return 0;
}

ZMK_LISTENER(fx_control_group_usb, fx_control_group_usb_listener);
ZMK_SUBSCRIPTION(fx_control_group_usb, zmk_usb_conn_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_USB) && DT_HAS_CHOSEN(zmk_rgb_fx) */
