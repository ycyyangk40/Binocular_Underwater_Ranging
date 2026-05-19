# CppOpenCVWebView

## Overview
This project provides a stereo underwater capture and processing pipeline based on OpenCV. It includes stereo calibration, rectification, disparity/depth estimation, and optional YOLO detection with ONNX Runtime. A Win32 + WebView2 UI is also included to control the CLI and show status.

Outputs:
- CLI: `opencv_cli`
- UI: `opencv_ui`

## Requirements
- Windows
- CMake 3.20+
- Visual Studio 2022 (or another MSVC toolset)
- OpenCV (set `OpenCV_DIR` to your OpenCV build folder)
- WebView2 SDK (auto-downloaded via CMake)
- Optional: ONNX Runtime for YOLO (`ENABLE_YOLO=ON` and `ONNXRUNTIME_ROOT`)

## Build (CMake)
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOpenCV_DIR=D:/OpenCV/opencv/build
cmake --build build --config Debug
```

### Enable YOLO
Place ONNX Runtime under a known path and point CMake to it:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DOpenCV_DIR=D:/OpenCV/opencv/build ^
  -DENABLE_YOLO=ON ^
  -DONNXRUNTIME_ROOT=D:/path/to/onnxruntime
cmake --build build --config Debug
```

## Run
### CLI
```bash
opencv_cli.exe 0 depth
```

### UI
```bash
opencv_ui.exe
```

## CLI Modes
- `calib`: interactive stereo calibration
- `rectify`: show rectified stereo
- `depth`: compute disparity and depth map
- `yolo`: YOLO detection with depth fusion (requires ONNX Runtime)
- `video`: basic stereo preview (default)

## CLI Arguments
Usage: `opencv_cli.exe [camera] [mode] [options]`

Options:
- `--mode <name>`
- `--view left|right|both`
- `--binary`
- `--fast`
- `--gray`
- `--no-enhance`
- `--show-left` | `--hide-left`
- `--show-right` | `--hide-right`
- `--show-disp` | `--hide-disp`
- `--show-depth` | `--hide-depth`
- `--model <path>`
- `--board-height <n>`
- `--board-width <n>`
- `--square-size <meters>`

## Calibration Outputs
Calibration data is stored under `data/`:
- `data/left_intrinsics.yml`
- `data/right_intrinsics.yml`
- `data/stereo.yml`

These files are required for `rectify`, `depth`, and `yolo`.

## Model Files
Default model path:
- `models/yolov8l.onnx`

You can override this with `--model <path>`.

## Project Structure
- `main.cpp`: CLI entry
- `src/mode_*.cpp`: mode implementations
- `src/webview_main.cpp`: UI entry
- `src/webview_app.cpp`: WebView2 host
- `web/`: UI assets

## Notes
- OpenCV and ONNX Runtime DLLs are copied to the build output directory after build.
- WebView2 SDK is downloaded automatically during CMake configure.

