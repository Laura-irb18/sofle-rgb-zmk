/*
 * SPDX-License-Identifier: MIT
 *
 * Heatmap effect: each key lights up when pressed and fades out,
 * leaving a typing trail. Only reacts to keys on its own half.
 */

#define DT_DRV_COMPAT zmk_rgb_fx_heatmap

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/rgb_fx.h>

#include <zmk/rgb_fx.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct fx_heatmap_config {
    struct zmk_color_hsl *color_hsl;
    size_t *pixel_map;
    size_t pixel_map_size;
    float *heat;
    uint8_t blending_mode;
    float decay_per_frame;
};

struct fx_heatmap_data {
    struct zmk_color_rgb color_rgb;
    bool is_active;
};

static int fx_heatmap_on_key_press(const struct device *dev, const zmk_event_t *event) {
    const struct fx_heatmap_config *config = dev->config;
    struct fx_heatmap_data *data = dev->data;

    const struct zmk_position_state_changed *pos_event;

    if (!data->is_active) {
        return 0;
    }

    if ((pos_event = as_zmk_position_state_changed(event)) == NULL) {
        return -ENOTSUP;
    }

    if (!pos_event->state) {
        return 0;
    }

    if (pos_event->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return 0;
    }

    size_t pixel_id = zmk_rgb_fx_get_pixel_by_key_position(pos_event->position);

    for (size_t j = 0; j < config->pixel_map_size; j++) {
        if (config->pixel_map[j] == pixel_id) {
            config->heat[j] = 1.0f;
            break;
        }
    }

    zmk_rgb_fx_request_frames(1);

    return 0;
}

static void fx_heatmap_render_frame(const struct device *dev, struct rgb_fx_pixel *pixels,
                                    size_t num_pixels) {
    const struct fx_heatmap_config *config = dev->config;
    struct fx_heatmap_data *data = dev->data;

    /* Reconvert per frame so the global hue offset applies. */
    zmk_hsl_to_rgb(config->color_hsl, &data->color_rgb);

    bool any_hot = false;

    for (size_t j = 0; j < config->pixel_map_size; j++) {
        float heat = config->heat[j];

        if (heat <= 0.0f) {
            continue;
        }

        struct zmk_color_rgb color = {
            .r = data->color_rgb.r * heat,
            .g = data->color_rgb.g * heat,
            .b = data->color_rgb.b * heat,
        };

        pixels[config->pixel_map[j]].value = zmk_apply_blending_mode(
            pixels[config->pixel_map[j]].value, color, config->blending_mode);

        config->heat[j] = heat - config->decay_per_frame;

        if (config->heat[j] > 0.0f) {
            any_hot = true;
        }
    }

    if (any_hot) {
        zmk_rgb_fx_request_frames(1);
    }
}

static void fx_heatmap_start(const struct device *dev) {
    struct fx_heatmap_data *data = dev->data;

    data->is_active = true;
}

static void fx_heatmap_stop(const struct device *dev) {
    const struct fx_heatmap_config *config = dev->config;
    struct fx_heatmap_data *data = dev->data;

    data->is_active = false;

    for (size_t j = 0; j < config->pixel_map_size; j++) {
        config->heat[j] = 0.0f;
    }
}

static int fx_heatmap_init(const struct device *dev) { return 0; }

static const struct rgb_fx_api fx_heatmap_api = {
    .on_start = fx_heatmap_start,
    .on_stop = fx_heatmap_stop,
    .render_frame = fx_heatmap_render_frame,
};

#define FX_HEATMAP_DEVICE(idx)                                                                     \
                                                                                                   \
    static size_t fx_heatmap_##idx##_pixel_map[] = DT_INST_PROP(idx, pixels);                      \
                                                                                                   \
    static float fx_heatmap_##idx##_heat[DT_INST_PROP_LEN(idx, pixels)];                           \
                                                                                                   \
    static uint32_t fx_heatmap_##idx##_color = DT_INST_PROP(idx, color);                           \
                                                                                                   \
    static struct fx_heatmap_config fx_heatmap_##idx##_config = {                                  \
        .color_hsl = (struct zmk_color_hsl *)&fx_heatmap_##idx##_color,                            \
        .pixel_map = &fx_heatmap_##idx##_pixel_map[0],                                             \
        .pixel_map_size = DT_INST_PROP_LEN(idx, pixels),                                           \
        .heat = &fx_heatmap_##idx##_heat[0],                                                       \
        .blending_mode = DT_INST_ENUM_IDX(idx, blending_mode),                                     \
        .decay_per_frame =                                                                         \
            (float)(1000 / CONFIG_ZMK_RGB_FX_FPS) / (float)DT_INST_PROP(idx, duration),            \
    };                                                                                             \
                                                                                                   \
    static struct fx_heatmap_data fx_heatmap_##idx##_data;                                         \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(idx, &fx_heatmap_init, NULL, &fx_heatmap_##idx##_data,                   \
                          &fx_heatmap_##idx##_config, POST_KERNEL,                                 \
                          CONFIG_APPLICATION_INIT_PRIORITY, &fx_heatmap_api);                      \
                                                                                                   \
    static int fx_heatmap_##idx##_event_handler(const zmk_event_t *event) {                        \
        const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(idx));                                \
                                                                                                   \
        return fx_heatmap_on_key_press(dev, event);                                                \
    }                                                                                              \
                                                                                                   \
    ZMK_LISTENER(fx_heatmap_##idx, fx_heatmap_##idx##_event_handler);                              \
    ZMK_SUBSCRIPTION(fx_heatmap_##idx, zmk_position_state_changed);

DT_INST_FOREACH_STATUS_OKAY(FX_HEATMAP_DEVICE);
