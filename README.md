# CppOpenCVWebView

## 概述
本项目提供一套基于 OpenCV 的双目水下采集与处理流程，包含双目标定、极线校正、视差/深度估计，并可选集成基于 ONNX Runtime 的 YOLO 检测。同时提供 Win32 + WebView2 的桌面 UI，用于控制 CLI 并显示运行状态。

产物:
- CLI: `opencv_cli`
- UI: `opencv_ui`

## 环境与依赖
- Windows
- CMake 3.20+
- Visual Studio 2022 (或其他 MSVC 工具链)
- OpenCV (通过 `OpenCV_DIR` 指向你的 OpenCV build 目录)
- WebView2 SDK (CMake 自动下载)
- 可选: ONNX Runtime (YOLO 支持, 需 `ENABLE_YOLO=ON` 与 `ONNXRUNTIME_ROOT`)

## 构建 (CMake)
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOpenCV_DIR=D:/OpenCV/opencv/build
cmake --build build --config Debug
```

### 启用 YOLO
将 ONNX Runtime 放在已知路径, 并在 CMake 中指定:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DOpenCV_DIR=D:/OpenCV/opencv/build ^
  -DENABLE_YOLO=ON ^
  -DONNXRUNTIME_ROOT=D:/path/to/onnxruntime
cmake --build build --config Debug
```

## 运行
### CLI
```bash
opencv_cli.exe 0 depth
```

### UI
```bash
opencv_ui.exe
```

## CLI 模式
- `calib`: 交互式双目标定
- `rectify`: 显示极线校正后的双目画面
- `depth`: 计算视差与深度图
- `yolo`: YOLO 检测并融合深度信息 (需要 ONNX Runtime)
- `video`: 基础双目预览 (默认)

## CLI 参数
用法: `opencv_cli.exe [camera] [mode] [options]`

参数:
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

## 标定输出
标定数据保存在 `data/` 下:
- `data/left_intrinsics.yml`
- `data/right_intrinsics.yml`
- `data/stereo.yml`

`rectify`、`depth`、`yolo` 需要这些文件。

## 模型文件
默认模型路径:
- `models/yolov8l.onnx`

可通过 `--model <path>` 覆盖。

## 项目结构
- `main.cpp`: CLI 入口
- `src/mode_*.cpp`: 模式实现
- `src/webview_main.cpp`: UI 入口
- `src/webview_app.cpp`: WebView2 宿主
- `web/`: UI 资源

## 说明
- OpenCV 与 ONNX Runtime 的 DLL 会在构建后复制到输出目录。
- WebView2 SDK 在 CMake configure 阶段自动下载。

