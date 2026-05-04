# Edifier — An Editor Application

A lightweight, dockable, cross-platform File Editor application built with **C++**, **Dear ImGui (docking branch)**, and **GLFW/OpenGL**. Designed for fast file-editing, searching, and quick modifications. This repository contains the core app (Notifier v1.0.0), with a polished UI, keyboard shortcuts, dockable panels, and persistent layout and notes storage.

> **Status:** Working — feature-complete for core functionality. Theme/background improvements planned.

---

## Features

* Dockable UI using Dear ImGui docking branch (tabs, split panels)
* Left resizable **Files List** (searchable, selectable, context menu)
* Center **Editor** with multiline editing and simple stats (words/characters)
* Keyboard shortcuts: `Ctrl+N`, `Ctrl+S`, `F5`, `Del`, `Esc`
* Persistent ImGui dock/layout state (via ImGui `.ini` file)
* Theme support: Dark / Light / Custom (customizable colors)
* Context menus, confirmation dialogs, and modest UX polish

---

## Screenshot

Custom Theme by default

### Custom Theme
![Notifier Screenshot](vendor/images/custom.png)

### Dark Theme
![Dark Theme](vendor/images/dark_theme.png)

### Light Theme
![Light Theme](vendor/images/light_theme.png)

---

## Quick Start (Linux Mint / Ubuntu)

(Windows instructions not included — I mainly use Linux. I'm sure it will work on windows with a few adjustments PRs welcome!)

### Install build dependencies

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config libglfw3-dev libglm-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
```

### Clone ImGui (docking branch)

The project relies on the **docking** branch of Dear ImGui to use the DockBuilder APIs.

```bash
git clone https://github.com/ocornut/imgui.git resources/imgui
cd resources/imgui
git fetch origin
git checkout docking
```

### Build the app

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
cd bin
./Notifier
```

---

## Project Layout / Key Files

* `main.cpp` — main application, UI rendering, app state and logic.
* `imgui/` (external) — Dear ImGui (docking branch) and backends
* `backends/` — `imgui_impl_glfw.cpp`, `imgui_impl_opengl3.cpp` (from ImGui examples)
* `fonts/` — optional custom fonts (e.g. `JetBrainsMonoNerdFontMono-Bold.ttf`)

---


## Important Implementations left to add

* **Proper Font Management.**


## UX & Visual Improvements

### What I plan to add maybe someday

* **Static background image** + subtle gradient tint and vignette.

* **Parallax** based on mouse position for depth.

* Use a **soft radial vignette mask** to focus the UI center.

* **Animated gradient** (slow color shifts) for a living UI.

* **Pre-blurred background** for glassmorphism (cheap approach).

* **Runtime Gaussian blur** (FBO + separable blur shader) to blur the area behind panels.

* **Particles / bokeh** subtle motion for polish.

---

## Troubleshooting

* **Buttons not clickable in docked windows:** make sure the dockspace host window doesn’t consume input or draw an opaque background. Use a fullscreen host window for the dockspace and set appropriate flags. Avoid `ImGuiWindowFlags_NoInputs` on the host if you want menus to be clickable inside it.

* **DockBuilder functions not found:** verify that you are using the **docking** branch of Dear ImGui and included `imgui_internal.h` only when necessary.

* **Fonts not loading:** confirm font path and file permissions. Consider bundling fonts under `fonts/` and using relative paths.

---

## Contributions

Contributions, PRs, and suggestions are welcome. If you add features, please:

* Keep ImGui usage idiomatic and avoid heavy per-frame allocations
* Add tests for parsing and saving notes if you add more complex storage formats
* Feel free to tinker and suggest ideas or improvements to me, would mean a lot.

---

## License & Credits

* This project builds on:

  * [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) — MIT-style license
  * [GLFW](https://www.glfw.org/) — zlib/libpng license
  * [stb_image](https://github.com/nothings/stb.git) — MIT-style license

Credits: 
        Safal — original developer and UI designer of Notifier.
---