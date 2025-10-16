# 海康威视 MVS SDK 摄像头接入程序

这是一个基于海康威视 Machine Vision SDK (MVS SDK) 的摄像头采集程序，支持 GigE 和 USB3.0 工业相机。

## 功能特性

- ✅ 自动枚举所有可用相机设备
- ✅ 支持 GigE 和 USB3.0 相机
- ✅ 连续图像采集
- ✅ 自动图像格式转换（转换为 BGR 格式）
- ✅ 实时帧率统计
- ✅ OpenCV 图像显示（可选）
- ✅ 相机参数设置（曝光、增益等）
- ✅ 图像保存功能
- ✅ 装甲板图像识别（基于 ONNX，需启用 OpenCV DNN）
- ✅ 优雅退出机制

## 系统要求

- Linux 操作系统
- CMake 3.10+
- C++14 编译器（支持 GCC 或 Clang）
- 海康威视 MVS SDK
- OpenCV（可选，用于图像显示）

## 安装依赖

### 1. 安装海康威视 MVS SDK

从海康威视官网下载适用于 Linux 的 MVS SDK：
https://www.hikvision.com/cn/support/download/sdk/

解压后运行安装脚本：
```bash
cd MVS-*
sudo ./setup.sh
```

默认安装路径：`/opt/MVS`

### 2. 安装 OpenCV（可选）

```bash
# Ubuntu/Debian
sudo apt-get install libopencv-dev

# 或者从源码编译
# https://docs.opencv.org/master/d7/d9f/tutorial_linux_install.html
```

## 编译

### 使用 CMake 命令行编译

```bash
# 创建构建目录
mkdir -p build
cd build

# 配置项目（如果 MVS SDK 安装在非默认路径，需要指定）
cmake .. -DMVS_SDK_PATH=/opt/MVS

# 编译
cmake --build .

# 或者使用 make
make -j$(nproc)
```

### 使用 CMake Presets（VS Code）

项目已配置 CMake Presets，可直接在 VS Code 中：
1. 按 `Ctrl+Shift+P` 打开命令面板
2. 选择 "CMake: Select Configure Preset"
3. 选择 "clang" preset
4. 按 `F7` 或点击状态栏的 Build 按钮

## 运行

```bash
# 运行程序（使用第一个检测到的相机）
./hiko

# 指定相机索引
./hiko 0    # 使用索引为 0 的相机
./hiko 1    # 使用索引为 1 的相机
```

### 装甲板图像识别

程序在启用 OpenCV 的情况下会加载 `model/resnet_best_embedded.onnx` 与 `labels.txt`，对透视矫正后的 `warpedArmor` 图像执行分类，并将类别及置信度叠加在实时预览和 "Armor Front View" 窗口中。

- 若模型文件位于其他位置，可通过环境变量 `HIKO_MODEL_DIR` 指定搜索目录：

  ```bash
  export HIKO_MODEL_DIR=/path/to/model_dir
  ./hiko
  ```

- 找不到模型或标签文件时会自动跳过识别流程，其余图像处理仍可正常运行。
- 模型输出的标签来自 `labels.txt`，可以根据训练数据自行调整。

### 运行时控制

- `Ctrl+C` 或 `ESC` 或 `Q`: 退出程序
- `S`: 保存当前帧为图像文件（需要 OpenCV）

## 项目结构

```
.
├── CMakeLists.txt          # CMake 配置文件
├── CMakePresets.json       # CMake 预设配置
├── ArmorMatcher.h          # 装甲板匹配库头文件
├── ArmorMatcher.cpp        # 装甲板匹配库实现
├── HikCamera.h             # 相机类头文件
├── HikCamera.cpp           # 相机类实现
├── main.cpp                # 主程序
├── README.md               # 本文档
└── build.sh                # 快速构建脚本
```

## API 使用示例

### 枚举设备

```cpp
std::vector<hik::CameraInfo> devices = hik::HikCamera::EnumerateDevices();
for (size_t i = 0; i < devices.size(); i++) {
    std::cout << "设备 " << i << ": " 
              << devices[i].modelName 
              << " (" << devices[i].serialNumber << ")" 
              << std::endl;
}
```

### 打开相机并采集图像

```cpp
hik::HikCamera camera;

// 打开第一个相机
if (!camera.Open(0)) {
    std::cerr << "打开相机失败: " << camera.GetLastError() << std::endl;
    return -1;
}

// 设置参数
camera.SetExposureTime(10000.0f);  // 10ms
camera.SetGain(10.0f);              // 10dB

// 开始采集
camera.StartGrabbing();

// 获取图像
hik::ImageData imageData;
if (camera.GrabImageBGR(imageData, 1000)) {
    // 处理图像数据
    // imageData.data: BGR 格式图像数据
    // imageData.width, imageData.height: 图像尺寸
}

// 停止并关闭
camera.StopGrabbing();
camera.Close();
```

### 设置触发模式

```cpp
// 启用硬件触发模式
camera.SetTriggerMode(true);

// 软件触发采集一帧
camera.TriggerSoftware();
```

## 常见问题

### 1. 找不到相机

- 检查相机是否正确连接并供电
- 对于 GigE 相机，确保网络配置正确：
  - 相机和电脑必须在同一网段
  - 建议使用专用网卡连接相机
  - 关闭防火墙或添加例外规则
- 运行 MVS SDK 提供的 MVS 客户端软件检测

### 2. 编译错误：找不到 MvCameraControl.h

确保 CMakeLists.txt 中的 MVS_SDK_PATH 设置正确：
```bash
cmake .. -DMVS_SDK_PATH=/path/to/your/MVS
```

### 3. 运行时错误：无法加载共享库

添加库路径到 LD_LIBRARY_PATH：
```bash
export LD_LIBRARY_PATH=/opt/MVS/lib/64:$LD_LIBRARY_PATH
```

或者在 /etc/ld.so.conf.d/ 中创建配置文件：
```bash
echo "/opt/MVS/lib/64" | sudo tee /etc/ld.so.conf.d/mvs.conf
sudo ldconfig
```

### 4. GigE 相机连接超时

- 增加网卡接收缓冲区大小：
  ```bash
  sudo ifconfig eth0 mtu 9000  # 启用 Jumbo Frame
  ```
- 调整包大小参数（在代码中添加）：
  ```cpp
  camera.SetIntValue("GevSCPSPacketSize", 8164);
  ```

## 相机参数说明

### 曝光时间 (ExposureTime)
- 单位：微秒 (μs)
- 范围：取决于相机型号
- 作用：控制图像亮度，曝光时间越长图像越亮

### 增益 (Gain)
- 单位：dB
- 范围：取决于相机型号
- 作用：放大图像信号，增益越大图像越亮但噪声也越大

### 触发模式 (TriggerMode)
- Off (0)：连续采集模式
- On (1)：触发模式，需要触发信号才采集

## 许可证

本项目仅供学习和参考使用。

## 参考资料

- [海康威视官网](https://www.hikvision.com/cn/)
- [MVS SDK 开发指南](https://www.hikvision.com/cn/support/download/)
- [OpenCV 文档](https://docs.opencv.org/)

## 联系方式

如有问题或建议，欢迎提交 Issue。
