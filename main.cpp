#include "HikCamera.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef USE_OPENCV
#include "ArmorMatcher.h"
#include "process.h"
#include <opencv2/opencv.hpp>
#endif

// 全局标志，用于优雅退出
std::atomic<bool> g_running(true);

// 信号处理函数
void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << "\nReceived signal " << signal << ", stopping..." << std::endl;
        g_running = false;
    }
}

int main(int argc, char *argv[])
{
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 枚举设备
    // std::cout << "正在枚举摄像头设备..." << std::endl;
    std::vector<hik::CameraInfo> devices = hik::HikCamera::EnumerateDevices();

    if (devices.empty())
    {
        std::cerr << "未找到任何摄像头设备！" << std::endl;
        std::cerr << "请检查：" << std::endl;
        std::cerr << "  1. 摄像头是否正确连接" << std::endl;
        std::cerr << "  2. 网络配置是否正确（GigE 相机）" << std::endl;
        std::cerr << "  3. MVS SDK 是否正确安装" << std::endl;
        return -1;
    }

    // 创建相机对象
    hik::HikCamera camera;

    // 打开第一个设备（可以通过命令行参数指定设备索引）
    unsigned int deviceIndex = 0;
    if (argc > 1)
    {
        deviceIndex = std::atoi(argv[1]);
        if (deviceIndex >= devices.size())
        {
            std::cerr << "无效的设备索引: " << deviceIndex << std::endl;
            return -1;
        }
    }
    camera.Open(deviceIndex);
    // 打印相机能力以便调优传输参数
    camera.PrintCameraCapabilities();
    // std::cout << "\n正在打开摄像头 [" << deviceIndex << "]..." << std::endl;
    // if (!camera.Open(deviceIndex)) {
    //     std::cerr << "打开摄像头失败: " << camera.GetLastError() << std::endl;
    //     return -1;
    // }

    // 获取图像尺寸
    unsigned int width = camera.GetWidth();
    unsigned int height = camera.GetHeight();
    std::cout << "图像尺寸: " << width << " x " << height << std::endl;

    // 设置相机参数（可选）
    std::cout << "\n设置相机参数..." << std::endl;

    // 设置曝光时间（微秒）- 增加曝光时间以获得更亮的图像
    float exposureTime = 5000.0f; // 5ms (增加曝光时间)
    if (camera.SetExposureTime(exposureTime))
    {
        std::cout << "曝光时间: " << camera.GetExposureTime() << " us" << std::endl;
    }
    else
    {
        std::cout << "设置曝光时间失败，使用默认值" << std::endl;
    }

    // 设置增益 - 增加增益以获得更亮的图像
    float gain = 5.0f; // 增加增益
    if (camera.SetGain(gain))
    {
        std::cout << "增益: " << camera.GetGain() << " dB" << std::endl;
    }
    else
    {
        std::cout << "设置增益失败，使用默认值" << std::endl;
    }
#ifdef USE_OPENCV
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> searchRoots;
        auto appendIfValid = [&](const fs::path &candidate) {
            if (candidate.empty())
                return;
            std::error_code ec;
            fs::path normalized = fs::weakly_canonical(candidate, ec);
            const fs::path &pathToStore = ec ? candidate : normalized;
            if (std::find(searchRoots.begin(), searchRoots.end(), pathToStore) == searchRoots.end())
            {
                searchRoots.push_back(pathToStore);
            }
        };

        appendIfValid(fs::current_path());

        if (const char *envDir = std::getenv("HIKO_MODEL_DIR"))
        {
            appendIfValid(fs::path(envDir));
        }

        std::error_code exeEc;
        fs::path exePath = fs::weakly_canonical(fs::path(argv[0]), exeEc);
        fs::path exeDir = exeEc ? fs::path(argv[0]).parent_path() : exePath.parent_path();
        appendIfValid(exeDir);
        if (exeDir.has_parent_path())
        {
            appendIfValid(exeDir.parent_path());
        }

        // NOTE: 为了简化调试，下面直接使用硬编码的模型与标签绝对路径。
        // 如果你已在项目根目录创建了 labels.txt（默认我已生成 1..9），直接使用它：
        fs::path modelPath = "/home/skyworld/文档/hiko/model/resnet_best_embedded.fixed.onnx"; // use simplified ONNX
                                                                                               // by default

        if (fs::exists(modelPath))
        {
            // 使用写死的绝对路径 labels.txt（便于调试）
            std::string labelsPathStr = "/home/skyworld/文档/hiko/labels.txt";

            auto matcher = std::make_shared<armor::ArmorMatcher>();
            if (matcher->loadWithLabels(modelPath.string(), labelsPathStr))
            {
                armor::setGlobalArmorMatcher(matcher);
                std::cout << "装甲板匹配模型已加载: " << modelPath << std::endl;
                if (!labelsPathStr.empty())
                    std::cout << "使用标签文件: " << labelsPathStr << std::endl;
            }
            else
            {
                std::cerr << "装甲板匹配模型加载失败: " << matcher->lastError() << std::endl;
            }
        }
        else
        {
            std::cerr << "未找到指定的模型，请检查 main.cpp 中的硬编码路径: " << modelPath << std::endl;
        }
    }
#endif
    camera.SetPixelFormat(17301515);
    // 开始采集
    std::cout << "\n开始采集图像..." << std::endl;
    if (!camera.StartGrabbing())
    {
        std::cerr << "开始采集失败: " << camera.GetLastError() << std::endl;
        camera.Close();
        return -1;
    }

    // 帧率统计
    int frameCount = 0;
    int totalFrames = 0; // 添加总帧数统计
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastPrintTime = startTime;
    bool firstFrame = true; // 标记第一帧

#ifdef USE_OPENCV
    // 创建窗口
    const char *windowName = "Hikvision Camera";
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
#endif

    std::cout << "\n采集中... (按 Ctrl+C 退出)" << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    // 主采集循环
    while (g_running)
    {
        hik::ImageData imageData;

        // 获取图像（BGR格式，便于OpenCV处理）
        if (camera.GrabImageBGR(imageData, 1000))
        {
            frameCount++;
            totalFrames++;

            // 打印第一帧的详细信息
            if (firstFrame)
            {
                std::cout << "\n=== 第一帧图像信息 ===" << std::endl;
                std::cout << "分辨率: " << imageData.width << "x" << imageData.height << std::endl;
                std::cout << "数据大小: " << imageData.dataSize << " 字节" << std::endl;
                std::cout << "像素格式: 0x" << std::hex << imageData.pixelFormat << std::dec << std::endl;

                // 检查图像数据
                if (imageData.data != nullptr && imageData.dataSize > 0)
                {
                    // 计算图像的平均亮度（采样前100个像素）
                    unsigned long long sum = 0;
                    int sampleCount = std::min(300, (int)imageData.dataSize);
                    for (int i = 0; i < sampleCount; i++)
                    {
                        sum += imageData.data[i];
                    }
                    double avgBrightness = (double)sum / sampleCount;
                    std::cout << "前100像素平均值: " << avgBrightness << std::endl;

                    if (avgBrightness < 10)
                    {
                        std::cout << "⚠️  警告: 图像非常暗，建议增加曝光时间或增益" << std::endl;
                    }
                }
                else
                {
                    std::cout << "❌ 错误: 图像数据为空!" << std::endl;
                }
                std::cout << "=====================\n" << std::endl;
                firstFrame = false;
            }

            // 计算帧率
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastPrintTime).count();

            // 每秒打印一次统计信息
            if (elapsed >= 1000)
            {
                float fps = frameCount * 1000.0f / elapsed;
                std::cout << "总帧数: " << totalFrames << " | 当前帧率: " << std::fixed << std::setprecision(2) << fps
                          << " fps"
                          << " | 分辨率: " << imageData.width << "x" << imageData.height << std::endl;

                lastPrintTime = currentTime;
                frameCount = 0;
            }

#ifdef USE_OPENCV
            // 使用 OpenCV 显示图像并调用处理函数
            cv::Mat image(imageData.height, imageData.width, CV_8UC3, imageData.data);

            // 克隆图像数据以避免数据失效
            cv::Mat displayImage = image.clone();

            // 缩放到 0.5 倍显示以节省窗口大小和渲染开销
            cv::Mat scaled;
            cv::resize(displayImage, scaled, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);

            // 调用处理模块（处理使用缩放后的图像以降低 CPU 开销）
            cv::Mat binaryOut, detected;
            processFrame(scaled, binaryOut, detected);

            // 确保二值图为单通道并转换为 BGR 以便并排显示
            cv::Mat binaryBGR;
            if (!binaryOut.empty())
            {
                if (binaryOut.type() != CV_8UC1)
                    cv::cvtColor(binaryOut, binaryBGR, cv::COLOR_BGR2GRAY);
                else
                    cv::cvtColor(binaryOut, binaryBGR, cv::COLOR_GRAY2BGR);
            }
            else
            {
                // 如果二值图为空，使用黑图占位
                binaryBGR = cv::Mat::zeros(scaled.size(), CV_8UC3);
            }

            // 如果检测结果为空，使用缩放原图作为占位
            if (detected.empty())
                detected = scaled.clone();

            // 创建并排显示的图像：binary | detected
            cv::Mat combined;
            try
            {
                cv::hconcat(binaryBGR, detected, combined);
            }
            catch (const cv::Exception &e)
            {
                // 如果合并失败，则只显示缩放图
                combined = scaled;
            }

            cv::imshow(windowName, combined);

            // 处理键盘事件
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q')
            { // ESC 或 Q 键退出
                std::cout << "\n用户请求退出..." << std::endl;
                g_running = false;
            }
            else if (key == 's' || key == 'S')
            { // S 键保存图像
                std::string filename =
                    "capture_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".jpg";
                cv::imwrite(filename, displayImage);
                std::cout << "图像已保存: " << filename << std::endl;
            }
            else if (key == '+' || key == '=')
            { // + 键增加曝光
                float currentExposure = camera.GetExposureTime();
                float newExposure = currentExposure * 1.5f;
                camera.SetExposureTime(newExposure);
                std::cout << "曝光时间: " << currentExposure << " -> " << camera.GetExposureTime() << " us"
                          << std::endl;
            }
            else if (key == '-' || key == '_')
            { // - 键减少曝光
                float currentExposure = camera.GetExposureTime();
                float newExposure = currentExposure / 1.5f;
                camera.SetExposureTime(newExposure);
                std::cout << "曝光时间: " << currentExposure << " -> " << camera.GetExposureTime() << " us"
                          << std::endl;
            }
#else
            // 如果没有 OpenCV，可以选择保存图像或进行其他处理
            // 这里添加一个小延时避免 CPU 占用过高
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
#endif
        }
        else
        {
            // 获取图像失败，尝试重连一次
            std::cerr << "GrabImage 失败，尝试重连..." << std::endl;
            const int maxRetries = 5;
            const int retryDelayMs = 500; // ms
            if (camera.Reconnect(deviceIndex, maxRetries, retryDelayMs))
            {
                std::cout << "重连成功，继续采集" << std::endl;
                continue;
            }
            else
            {
                std::cerr << "重连失败，短暂休眠后重试主循环" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    std::cout << "\n-----------------------------------" << std::endl;
    std::cout << "停止采集..." << std::endl;

    // 停止采集
    camera.StopGrabbing();

    // 关闭相机
    camera.Close();

#ifdef USE_OPENCV
    cv::destroyAllWindows();
#endif

    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
