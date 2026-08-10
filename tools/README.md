# Drastic Layout Editor

This is a standalone Windows layout editor for the Drastic SDL hook layout format.
It edits and exports `layout.json` only. Runtime options such as transparent alpha,
transparent overlay position, theme selection, pixel filter, and FPS display are
still controlled by the emulator settings menu and `resources/settings.json`.

## Features

- Set the target screen resolution.
- Switch the UI language between English and Simplified Chinese.
- Add, duplicate, delete, and rename layouts.
- Drag `screen0` and `screen1` on a scaled preview canvas.
- Resize a screen from its bottom-right handle.
- Keep NDS screens locked to 4:3 while resizing.
- Edit exact pixel coordinates and sizes.
- Set `type`, `rotate`, `name`, and `bg`.
- Import existing `layout.json` files.
- Export a compatible `layout.json`.

## Build

Dependencies:

- CMake 3.16 or newer
- A C++17 compiler
- A Dear ImGui checkout
- Windows SDK with DirectX 11 headers and libraries

The first version uses Dear ImGui with the Win32 + DirectX 11 backends. It does
not depend on SDL2.

For Simplified Chinese UI, the editor tries to load a Windows system CJK font
from `C:\Windows\Fonts`, such as Microsoft YaHei.

When cross-compiling with MinGW-w64, the tool links against the Windows system
libraries `d3d11`, `dxgi`, `d3dcompiler`, `dwmapi`, and `imm32`.

Example:

```bat
cmake -S tools/layout_editor -B build-layout-editor ^
  -DIMGUI_DIR=C:\dev\imgui

cmake --build build-layout-editor --config Release
```

WSL cross-compile example:

```sh
sudo apt-get install -y mingw-w64 cmake ninja-build git
git clone https://github.com/ocornut/imgui.git /tmp/imgui

cmake -S tools/layout_editor -B build-layout-editor-mingw -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DIMGUI_DIR=/tmp/imgui

cmake --build build-layout-editor-mingw
```

The output executable is:

```text
build-layout-editor\Release\drastic_layout_editor.exe
```

For the WSL cross-compile example, the output is:

```text
build-layout-editor-mingw/drastic_layout_editor.exe
```

## Dear ImGui

The tool does not vendor Dear ImGui. Download or clone Dear ImGui separately,
then pass its folder through `-DIMGUI_DIR=...`.

The expected folder layout is:

```text
imgui/
  imgui.cpp
  imgui_draw.cpp
  imgui_tables.cpp
  imgui_widgets.cpp
  backends/
    imgui_impl_win32.cpp
    imgui_impl_dx11.cpp
```

## Usage

1. Enter the target resolution, for example `1920 x 1080`.
2. Select or add a layout.
3. Drag a screen rectangle to move it.
4. Drag the bottom-right white handle to resize it.
5. Use the right panel for exact values and layout properties.
6. Click `Load` to choose an existing `layout.json`, or click `Save` to choose where to write one.

The editor starts with an in-memory default layout and does not write any files
until `Save` is used.

For Drastic, place the exported file here:

```text
/storage/.config/drastic/resources/bg/<width>x<height>/layout.json
```

For example:

```text
/storage/.config/drastic/resources/bg/1920x1080/layout.json
```

Background images are resolved relative to the exported `layout.json` path,
using the first theme directory:

```text
<layout.json directory>/1/<bg>
```

For example, if the layout is saved to:

```text
resources/bg/1920x1080/layout.json
```

and the selected layout has `"bg": "normal.png"`, the editor previews:

```text
resources/bg/1920x1080/1/normal.png
```

When saving, if a layout has an empty `bg` value, the editor assigns a default
PNG name from the layout name, such as `normal.png`. If the corresponding file
does not exist, the editor generates a PNG mask at that path. The generated mask
is black outside the screen rectangles and transparent inside them.

## Format

The exported file uses the schema currently parsed by `src/render/jsonlayout.c`:

```json
{
  "name": "Drastic Layout",
  "layout": [
    {
      "index": 0,
      "type": 0,
      "name": "Normal",
      "bg": "",
      "rotate": 0,
      "screen0_x": 64,
      "screen0_y": 60,
      "screen0_w": 1280,
      "screen0_h": 960,
      "screen1_x": 1408,
      "screen1_y": 372,
      "screen1_w": 448,
      "screen1_h": 336
    }
  ]
}
```

Transparent mode (`type = 1`) still uses `screen1_w` and `screen1_h` as the
bottom screen size. Its actual corner position and alpha are runtime settings.

The editor exposes the three layout types that have distinct behavior in the
hook:

- `0`: dual screen
- `1`: transparent overlay
- `4`: single screen

Templates such as side-by-side, vertical stack, and top-full only change the
screen rectangles. They are exported as `type = 0` unless the transparent or
single-screen template is selected.
