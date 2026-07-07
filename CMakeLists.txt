# SPDX-License-Identifier: MIT

if(CONFIG_CODEKEEB_BATTERY_NRF_VDDH_CURVE)
  zephyr_library()
  zephyr_library_sources(src/battery_nrf_vddh_curve.c)
endif()

if(CONFIG_ZMK_RGB_FX)
  target_include_directories(app PRIVATE include)
  target_sources(app PRIVATE
    src/color.c
    src/rgb_fx.c
    src/behaviors/behavior_rgb_fx.c
    src/fx/control_group.c
    src/fx/ripple.c
    src/fx/sparkle.c
    src/fx/linear_gradient.c
    src/fx/solid.c
    src/fx/heatmap.c
    src/fx/layer_color.c)
endif()
