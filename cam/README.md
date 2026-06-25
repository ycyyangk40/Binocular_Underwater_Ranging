# Cam — Jetson 双目水下测距

自包含的 Jetson (ARM Linux) 部署文件夹。在 Windows 上开发、在本目录整理好之后直接传输到 Jetson 上编译运行。

## 目录结构

```
cam/
├── CMakeLists.txt          # Jetson 适配的 CMake 构建文件
├── main.cpp                # CLI 入口，7 种模式
├── src/                    # 所有源码
│   ├── common.cpp/hpp      # 公共工具（加载 YAML 等）
│   ├── mode_calib.cpp/hpp  # 棋盘标定模式
│   ├── mode_depth.cpp/hpp  # 双目深度模式（SGBM/BM）
│   ├── mode_rectify.cpp/hpp# 双目校正预览模式
│   ├── mode_sonar.cpp/hpp  # 超声波传感器测试模式
│   ├── mode_video.cpp/hpp  # 视频捕获测试模式
│   ├── mode_yolo.cpp/hpp   # YOLO+双目深度模式
│   └── sonar_reader.cpp/hpp# 超声波 Modbus RTU 串口读取
├── data/                   # 标定文件（必须）
│   ├── left_intrinsics.yml
│   ├── right_intrinsics.yml
│   └── stereo.yml
├── models/                 # ONNX 模型
│   └── yolov8n.onnx        # YOLOv8 Nano (12MB)
└── web/                    # 监控 Web UI
    ├── index.html
    ├── app.js
    └── styles.css
```

## 前置依赖

### 1. OpenCV (必须)

Jetson 通常自带 OpenCV 4.x（JetPack 刷机时预装）。验证：

```bash
# 检查是否已安装
dpkg -l | grep opencv
pkg-config --modversion opencv4

# 如果没有，通过 apt 安装
sudo apt update
sudo apt install -y libopencv-dev
```

### 2. ONNX Runtime (可选，YOLO 模式需要)

```bash
# 下载 Jetson ARM64 预编译包
# 注意版本号，以实际最新为准
cd ~
wget https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-aarch64-1.20.1.tgz
tar xzf onnxruntime-linux-aarch64-1.20.1.tgz
# 解压到 ~/onnxruntime-linux-aarch64-1.20.1/
# CMake 会自动检测此路径
```

不用 YOLO 模式可以跳过此步，CMake 会自动禁用。

### 3. CMake + 编译工具链

```bash
sudo apt install -y cmake build-essential
```

## 编译

```bash
cd ~/cam

# 默认构建（自动检测 ONNX Runtime）
mkdir build && cd build
cmake ..

# 如果 ONNX Runtime 未自动检测到，手动指定路径：
# cmake .. -DONNXRUNTIME_ROOT=~/onnxruntime-linux-aarch64-1.20.1

# 如果不需要 YOLO，显式禁用：
# cmake .. -DENABLE_YOLO=OFF

make -j$(nproc)
```

编译产物：`build/opencv_cli`

## 运行

所有模式共用同一个可执行文件，通过第二个参数切换模式。

```bash
cd ~/cam/build

# ===== 双目深度模式（默认）=====
./opencv_cli 0 depth

# 可选参数
./opencv_cli 0 depth --fast          # 快速模式（StereoBM 代替 StereoSGBM）
./opencv_cli 0 depth --no-enhance    # 关闭图像增强

# ===== YOLO + 双目深度模式 =====
./opencv_cli 0 yolo

# 指定模型路径
./opencv_cli 0 yolo --model ../models/yolov8n.onnx

# ===== 标定模式 =====
./opencv_cli 1 calib --board-width 9 --board-height 6 --square-size 2.97

# ===== 校正预览模式 =====
./opencv_cli 0 rectify

# ===== 视频捕获测试 =====
./opencv_cli 0 video
./opencv_cli 0 video --binary       # 二值化模式
./opencv_cli 0 video --gray         # 灰度模式

# ===== 超声波传感器测试 =====
./opencv_cli 0 sonar --sonar-com /dev/ttyUSB0 --sonar-baud 115200 --sonar-addr 1
./opencv_cli 0 sonar --sonar-com /dev/ttyUSB0 --sonar-debug   # 调试输出
```

### 参数说明

| 参数 | 适用模式 | 说明 |
|------|---------|------|
| `<camera_id>` | 全部 | 摄像头编号，通常是 0 或 1 |
| `<mode>` | 全部 | 模式名: depth / yolo / calib / rectify / video / sonar |
| `--fast` | depth, yolo | 快速匹配（BM 代替 SGBM） |
| `--no-enhance` | depth, yolo, video | 关闭 CLAHE 图像增强 |
| `--gray` | depth, yolo, video | 显示灰度图 |
| `--model <path>` | yolo | ONNX 模型路径 |
| `--sonar-com <port>` | sonar | 串口设备路径，如 `/dev/ttyUSB0` |
| `--sonar-baud <rate>` | sonar | 波特率，默认 115200 |
| `--sonar-addr <addr>` | sonar | Modbus 从机地址，默认 1 |
| `--sonar-debug` | sonar | 打印串口调试信息 |
| `--board-width <n>` | calib | 棋盘格内角点列数 |
| `--board-height <n>` | calib | 棋盘格内角点行数 |
| `--square-size <mm>` | calib | 棋盘格方格边长 (mm) |
| `--binary` | video | 二值化阈值模式 |

### 显示控制

| 参数 | 说明 |
|------|------|
| `--show-left` / `--hide-left` | 显示/隐藏左视图 |
| `--show-right` / `--hide-right` | 显示/隐藏右视图 |
| `--show-disp` / `--hide-disp` | 显示/隐藏视差图 |
| `--show-depth` / `--hide-depth` | 显示/隐藏深度图 |
| `--view left\|right\|both` | 快捷视图切换 |

## 串口权限（Sonar 模式）

```bash
# 将用户加入 dialout 组（需要重新登录生效）
sudo usermod -a -G dialout $USER

# 或者临时授权
sudo chmod 666 /dev/ttyUSB0
```

## 标定文件说明

`data/` 下的 3 个 YAML 文件是双目测距的核心：

- `left_intrinsics.yml` — 左摄像头内参矩阵和畸变系数
- `right_intrinsics.yml` — 右摄像头内参矩阵和畸变系数
- `stereo.yml` — 双目外参（R, T）+ 本征矩阵 + 基础矩阵

如果更换摄像头或重新标定，需要更新这 3 个文件。

## 常见问题

**Q: 运行报 `GStreamer: unable to start pipeline`**
A: 摄像头索引不对。尝试 `./opencv_cli 0 depth` / `./opencv_cli 1 depth` / `./opencv_cli 2 depth`。

**Q: YOLO 模式提示 "YOLO not available"**
A: 编译时未找到 ONNX Runtime。安装后重新 `cmake .. && make`。

**Q: 串口打开失败 `com_not_open`**
A: 检查设备路径 `ls /dev/ttyUSB*` 和权限 `sudo chmod 666 /dev/ttyUSB0`。

**Q: 视差图全黑**
A: 标定文件与当前摄像头不匹配，需要重新标定。
