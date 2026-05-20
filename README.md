# dsi-screen

A desktop pet app running on an 800×480 DSI touchscreen, built for Bianbu 2.3.3 (SpacemiT K1, RISC-V). Written in Qt5 with direct evdev input handling.

## Features

- **Three-screen swipe UI** — swipe down to go forward, swipe up to go back
- **Settings page** — live CPU usage, current time, 6-tile settings grid (个人中心 / WiFi / 蓝牙 / 通用设置 / 频道配置 / 频度查询)
- **Dragon desktop pet** — sprite sheet animation at 7 FPS
  - Idle loop by default
  - Single tap → heart animation (`idle2heart → heart → reverse → idle`)
  - Double tap → work mode for 15 seconds (`idle2work → work → reverse → idle`)
- **Light blue placeholder** — third screen, reserved for future use

## Hardware

| Item | Detail |
|---|---|
| Board | SpacemiT K1 (RISC-V) |
| OS | Bianbu 2.3.3 (Noble Numbat) |
| Display | 800×480 DSI (Raspberry Pi touchscreen, tc358762xbg) |
| Touch | FT5426 (`/dev/input/event0`), 1:1 pixel coordinates |
| Window system | labwc (Wayland) + lxqt-session |

## Project Structure

```
dsi_screen/
├── main.cpp          # All source code
└── dsi_screen.pro    # qmake project file
```

Sprite sheets (not included) go in `~/Desktop/dragon/`:

```
idle.png        — 18 frames, 5400×380
idle2heart.png  —  7 frames, 2100×380
heart.png       — 17 frames, 5100×380
idle2work.png   —  8 frames, 2400×380
work.png        — 11 frames, 3300×380
```

Each frame is 300×380 px, arranged horizontally.

## Build

Install dependencies:

```bash
sudo apt install qtbase5-dev qttools5-dev qttools5-dev-tools \
                 liblxqt2-dev lxqt-build-tools cmake build-essential
```

Build:

```bash
mkdir build && cd build
qmake ..
make -j4
```

## Run

```bash
WAYLAND_DISPLAY=wayland-0 \
XDG_RUNTIME_DIR=/run/user/1000 \
QT_QPA_PLATFORM=wayland \
sg input -c './build/dsi_screen'
```

> The `sg input` wrapper is required to read raw touch events from `/dev/input/event0`. Alternatively, add your user to the `input` group (`sudo usermod -aG input $USER`) and re-login.

## Notes

- Uses **qmake** instead of CMake — the Qt5 CMake config on this platform references a missing `libqxcb-glx-integration.so` (GLX not supported on RISC-V GLES-only builds)
- Touch input is read **directly from evdev**, bypassing Wayland's input dispatch (labwc does not forward touch events to Qt windows reliably)
- Swipe threshold: 30 px vertical movement; tap threshold: <30 px in both axes; double-tap window: 400 ms
