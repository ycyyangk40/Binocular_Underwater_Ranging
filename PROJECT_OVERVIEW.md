# 项目概述

## 简介
本项目提供一套基于 OpenCV 的双目水下采集与处理流程，包含标定、极线校正、视差/深度估计，并可选集成 YOLO 检测。同时提供基于 WebView2 的桌面 UI，用于控制 CLI 并显示运行状态。

## 主要目标
- 命令行程序 `opencv_cli`：负责相机采集、标定、深度/视差计算与 YOLO 推理。
- Windows UI 程序 `opencv_ui`：嵌入 WebView2 控制面板，启动/停止 CLI 并展示日志状态。

## 入口
- CLI 入口：[main.cpp](main.cpp)
- UI 入口：[src/webview_main.cpp](src/webview_main.cpp)

## CLI 模式
CLI 通过位置参数或 `--mode` 选择工作模式：
- `calib`：交互式双目标定。见 [src/mode_calib.cpp](src/mode_calib.cpp)。
- `rectify`：显示极线校正后的左右画面。见 [src/mode_rectify.cpp](src/mode_rectify.cpp)。
- `depth`：计算视差与深度图，支持点击测距。见 [src/mode_depth.cpp](src/mode_depth.cpp)。
- `yolo`：左目 YOLO 检测并融合深度信息，需要 ONNX Runtime。见 [src/mode_yolo.cpp](src/mode_yolo.cpp)。
- `video`（默认）：简单双目预览，可选灰度/二值化。见 [src/mode_video.cpp](src/mode_video.cpp)。

通用参数与默认值解析在 [main.cpp](main.cpp)。

## 标定输出
标定结果写入 data 目录：
- 左目内参：[data/left_intrinsics.yml](data/left_intrinsics.yml)
- 右目内参：[data/right_intrinsics.yml](data/right_intrinsics.yml)
- 双目外参：[data/stereo.yml](data/stereo.yml)

这些文件会被 depth/rectify/yolo 模式读取。

## Web UI
UI 为 WebView2 桌面应用，加载本地 HTML 控制面板并启动 CLI：
- 主程序与进程控制：[src/webview_app.cpp](src/webview_app.cpp)
- 前端资源：[web/index.html](web/index.html)，[web/app.js](web/app.js)，[web/styles.css](web/styles.css)

UI 通过 JSON 消息与宿主通信，并基于 CLI 日志流更新状态。

## 依赖
- OpenCV（通过 CMake 的 `OpenCV_DIR` 指定）：[CMakeLists.txt](CMakeLists.txt)
- WebView2 SDK（NuGet 自动拉取）：[CMakeLists.txt](CMakeLists.txt)
- 可选 ONNX Runtime（YOLO 支持，`ENABLE_YOLO`）：[CMakeLists.txt](CMakeLists.txt)
- 默认 YOLO 模型：[models/yolov8l.onnx](models/yolov8l.onnx)

## 技术栈
- 语言/标准：C++17（见 [CMakeLists.txt](CMakeLists.txt)）
- 计算机视觉：OpenCV（双目标定、校正、视差/深度）
- 推理引擎：ONNX Runtime（YOLO 推理，可选）
- UI 桌面框架：Win32 + WebView2（见 [src/webview_app.cpp](src/webview_app.cpp)）
- 前端：HTML/CSS/JavaScript（见 [web/index.html](web/index.html)，[web/styles.css](web/styles.css)，[web/app.js](web/app.js)）
- 构建：CMake（见 [CMakeLists.txt](CMakeLists.txt)）

## 构建产物
CMake 构建两个可执行文件：
- `opencv_cli.exe`
- `opencv_ui.exe`

OpenCV 与 ONNX Runtime 的运行时 DLL 会在构建后拷贝到输出目录，规则见 [CMakeLists.txt](CMakeLists.txt)。

## 典型用法
- 运行 UI（`opencv_ui.exe`）通过面板启动/停止各模式。
- 或直接运行 CLI：`opencv_cli.exe 0 depth`（相机索引 + 模式）。

## 关键源码
- 公共工具与图像增强：[src/common.cpp](src/common.cpp)，[src/common.hpp](src/common.hpp)
- 各模式实现：[src/mode_calib.cpp](src/mode_calib.cpp)，[src/mode_rectify.cpp](src/mode_rectify.cpp)，[src/mode_depth.cpp](src/mode_depth.cpp)，[src/mode_video.cpp](src/mode_video.cpp)，[src/mode_yolo.cpp](src/mode_yolo.cpp)
- WebView2 宿主：[src/webview_app.cpp](src/webview_app.cpp)，[src/webview_app.hpp](src/webview_app.hpp)
