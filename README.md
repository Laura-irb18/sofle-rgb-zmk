# Sofle RGB (MX) — ZMK firmware · CODE/KEEB

ZMK firmware for the CODE/KEEB **Sofle RGB (MX)**: nice!nano v2, vertical
OLED displays, 36 RGB LEDs per half (encoder indicator + underglow +
per-key), 2 rotary encoders and ZMK Studio.

It bundles a **custom RGB effect engine** (vendored and heavily extended
from [zmk-rgb-fx], MIT) and a **fork of the display module**
([codekeeb/zmk-nice-oled], `selectable` branch) with features that don't
exist in the originals.

> This is the **MX** variant. The Choc wireless variant lives in
> [codekeeb/sofle-choc-rgb-zmk](https://github.com/codekeeb/sofle-choc-rgb-zmk)
> and uses a different data pin (P0.08) and LED count (30).

---

## ⌨️ Controls

Same layout as the Choc variant: layers **CODE** (base), **Lower** and
**Raise**.

### Encoders

| Gesture                 | Base                 | Lower               | Raise        |
| ----------------------- | -------------------- | ------------------- | ------------ |
| **Turn left encoder**   | Volume               | RGB mode ±          | Brightness ± |
| **Turn right encoder**  | Scroll (Up/Down)     | Hue ±               | Speed ±      |
| **Press left encoder**  | Mute                 | RGB on/off          | —            |
| **Press right encoder** | Play/pause           | Next OLED animation | RGB on/off   |

### RGB keys (LOWER, left half)

| Key   | Function         |
| ----- | ---------------- |
| R     | RGB on/off       |
| T     | Next RGB mode    |
| F / G | Hue − / +        |
| V / B | Brightness − / + |

RAISE also holds the **numpad** (right half), the **Bluetooth panel**
(`BT_CLR` + profiles 1-5 on the top-left row) and clipboard keys
(undo/cut/copy/paste). ZMK Studio unlock is on LOWER + Z.

All RGB settings (mode, hue, brightness, speed, on/off) and the selected
OLED animation **persist in flash**: they survive power cycles and
reflashes (only `settings_reset` clears them).

- **ZMK Studio**: the left half ships Studio; connect over USB, open
  [zmk.studio](https://zmk.studio) to remap live without flashing.

---

## 🌈 RGB system

Coordinate-based animation engine: every LED has an (x,y) position on a
**continuous canvas from 0 to 240 spanning both halves**, so horizontal
gradients cross the seam without a jump. The maps live in
`config/sofle_left.overlay` and `config/sofle_right.overlay`.

### LED chain (36 per half)

From the josefadamcik Sofle RGB v4 PCB:

- **LED 1** = encoder indicator
- **LED 2-7** = underglow (drop lighting)
- **LED 8-36** = per-key (underlighting)

The overlays hold the (x,y) of each LED, so positional effects follow the
physical layout. The map was originally read from a photo of the **back**
of the PCB (mirrored); after confirming the real orientation on hardware,
both the x coordinates and every key→pixel table (overlays and
`layer_color.c`) now use the true left/right orientation.

### Modes (cycle with LOWER + left encoder, or LOWER + T)

| #  | Mode         | Description                                                 |
| -- | ------------ | ----------------------------------------------------------- |
| 1  | Gradient     | 3-color gradient animated diagonally                         |
| 2  | Ripple       | Blue waves from each pressed key                             |
| 3  | Sparkle      | Cyan/magenta sparkles                                        |
| 4  | Solid        | Uniform color cycling amber↔pink                             |
| 5  | Fire         | Vertical red-orange-yellow gradient, fast                    |
| 6  | Ocean        | Blue-cyan-green horizontal, very slow                        |
| 7  | Gold sparkle | Fast golden sparkles                                         |
| 8  | Pink ripple  | Faster, wider pink waves                                     |
| 9  | Sunset       | **Static**: orange-coral-pink-purple-blue (S100, tight arc)  |
| 10 | Heatmap      | Each key lights up when pressed and fades out (~1.2 s)       |

### Layer indicators

While a layer is held, fixed-color indicators are painted on top of the
active RGB mode (they don't rotate with the hue offset):

- **LOWER**: all thumb keys on both halves light up **pink**, and the
  **arrow cluster** (right half, I/J/K/L positions) lights up in **orange**.
- **RAISE**: all thumb keys light up **purple**, plus the **Bluetooth
  panel** (left half, top row): `BT_CLR` in **red**, profiles 1-5 in
  **yellow** with the **active profile in green**.

The layer state is relayed to the peripheral half over the split behavior
channel (`rgblay`), so both halves stay in sync. The key→LED lookup lives
in `src/fx/layer_color.c` and the `key-pixels` map (used by the ripple and
heatmap modes to hit the right LED) in both `config/sofle_*.overlay` files.

### Global controls

- **Hue**: a 0-359° offset applied in the HSL→RGB conversion — rotates the
  full palette of any mode without destroying it. 20° steps.
- **Brightness**: 4 steps — 10 / 40 / 70 / 100% (minimum 10%: turning off
  is the toggle's job).
- **Speed**: 5 steps (0.25×–4×) scaling the animation tick period.

### Split state sync

The RGB state (on/off switch, mode, hue, speed, brightness) is owned by
the **left (central) half**. Every RGB key/encoder command is handled
there and the resulting **absolute** state is pushed to the right half
over BLE — immediately after each command and every 15 s as a self-heal.
The halves can never drift apart (inverted toggles, different modes):
any divergence converges on the next push. USB power does **not** affect
the RGB — both halves always render the same shared state.

Defaults: the RGB starts **OFF** at every boot (the on/off switch is not
restored from flash). Turning it on (LOWER + R or the encoder click)
lights **both halves at 10%** brightness — raise it with RAISE + left
encoder. Mode, hue, speed and the OLED animation still persist.

### Editing palettes and speeds

Every mode is a node in both `sofle_*.overlay` files (edit BOTH!):
`colors = <HSL(hue, saturation, lightness) ...>` (hue 0-359, S=100 L=50 is
the purest color), `duration` (lower = faster), `angle` for gradients.

---

## 🖥️ OLED displays

Module: [codekeeb/zmk-nice-oled] `selectable` branch.

- **Left (central)**: graphic battery, BT/USB output, layer, profile, and
  **Bongo Cat** banging along to your WPM.
- **Right (peripheral)**: graphic battery + **runtime-switchable animation**
  (LOWER + press right encoder, persists): gem, cat, 3D head, spaceman,
  pokemon, CODE/KEEB logo.
- **Graphic battery**: battery icon with proportional fill + bolt while
  charging.

---

## 🔋 Battery

- **Real percentage**: custom driver (`src/battery_nrf_vddh_curve.c`) with
  an interpolated LiPo discharge curve (21 points) instead of ZMK's
  4.20→3.45 V straight line. The % depends only on voltage.
- `CONFIG_BOARD_ENABLE_DCDC_HV=y`: ZMK ≥ v0.3 disabled it by default; on
  boards with an inductor it's needed to power the 36 LEDs.

---

## 🔧 Hardware

| Property        | Value          |
| --------------- | -------------- |
| Board           | nice!nano v2   |
| LED data pin    | **P0.06**      |
| LEDs per half   | **36** (encoder + underglow + per-key) |
| PCB             | josefadamcik Sofle RGB v4 (MX) |

---

## 🏗️ Builds

Every push builds `sofle_left` (with ZMK Studio), `sofle_right` and
`settings_reset`, available as the `firmware` GitHub Actions artifact
(sign in to GitHub, 90-day expiry).

**Pinned versions** for reproducible builds (`config/west.yml`):
ZMK `v0.3`, and the [codekeeb/zmk-nice-oled] fork `selectable` branch.

### 📦 Releases (precompiled firmware)

Pushing a tag like `v1.0.0` also publishes a **GitHub Release** with the
same three `.uf2` files attached — no GitHub account needed to download.
See the [Releases page](https://github.com/codekeeb/sofle-rgb-zmk/releases)
for ready-to-flash firmware.

To cut a new release (maintainers):

```sh
git tag v1.0.0
git push origin v1.0.0
```

### Flashing

Double-tap reset → USB drive → copy the `.uf2`. After firmware changes
with weird state: `settings_reset` on both halves and reflash.

> The full ZMK fork this repo used to be lives on the `develop` branch and
> the `pre-restructure-backup` tag. `main` is now a clean user-config.

---

## Credits

- RGB engine based on [zmk-rgb-fx] by Kuba Birecki (MIT) — heavily modified.
- Displays based on [mctechnology17/zmk-nice-oled] (MIT) — via our own fork.
- Sofle RGB by Dane Evans, original by [josefadamcik](https://josefadamcik.github.io/SofleKeyboard/).
- [ZMK Firmware](https://zmk.dev) `v0.3`.

[zmk-rgb-fx]: https://github.com/crystalplanet/zmk-rgb-fx
[codekeeb/zmk-nice-oled]: https://github.com/codekeeb/zmk-nice-oled/tree/selectable
[mctechnology17/zmk-nice-oled]: https://github.com/mctechnology17/zmk-nice-oled
