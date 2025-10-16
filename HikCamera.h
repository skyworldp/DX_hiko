#ifndef HIK_CAMERA_H
#define HIK_CAMERA_H

#include "MvCameraControl.h"
#include <string>
#include <vector>

namespace hik
{

// 相机信息结构
struct CameraInfo
{
    std::string serialNumber;
    std::string modelName;
    std::string ipAddress;
    unsigned int deviceType;
};

// 图像数据结构
struct ImageData
{
    unsigned char *data;
    unsigned int width;
    unsigned int height;
    unsigned int pixelFormat;
    unsigned int dataSize;

    ImageData() : data(nullptr), width(0), height(0), pixelFormat(0), dataSize(0)
    {
    }
};

// 海康相机类
class HikCamera
{
  public:
    HikCamera();
    ~HikCamera();

    // 枚举所有可用相机
    static std::vector<CameraInfo> EnumerateDevices();

    // 打开相机 (通过索引)
    bool Open(unsigned int index = 0);

    // 打开相机 (通过序列号)
    bool OpenBySerialNumber(const std::string &serialNumber);

    // 关闭相机
    bool Close();

    // 开始采集
    bool StartGrabbing();

    // 停止采集
    bool StopGrabbing();

    // 获取一帧图像
    bool GrabImage(ImageData &imageData, unsigned int timeout = 1000);

    // 获取一帧图像并转换为BGR格式
    bool GrabImageBGR(ImageData &imageData, unsigned int timeout = 1000);

    // 设置曝光时间 (微秒)
    bool SetExposureTime(float exposureTime);

    // 获取曝光时间
    float GetExposureTime();

    // 设置增益
    bool SetGain(float gain);

    // 获取增益
    float GetGain();

    // 设置触发模式
    bool SetTriggerMode(bool enable);

    // 软件触发一次
    bool TriggerSoftware();

    // 设置帧率 (fps)，部分相机用 AcquisitionFrameRate
    bool SetFrameRate(float fps);

    // 获取帧率
    float GetFrameRate();

    // 设置像素格式 (使用 SDK 的像素格式常量，例如 PixelType_Gvsp_Mono8 等)
    bool SetPixelFormat(unsigned int pixelFormat);

    // 获取当前像素格式
    unsigned int GetPixelFormat();

    // 获取图像宽度
    unsigned int GetWidth();

    // 获取图像高度
    unsigned int GetHeight();

    // 传输层参数（可用于调优带宽）
    bool SetPacketSize(unsigned int packetSize);
    unsigned int GetPacketSize();

    bool SetPacketDelay(unsigned int packetDelay);
    unsigned int GetPacketDelay();

    // 读取相机实际输出/结果帧率（ResultingFrameRate）
    float GetResultingFrameRate();

    // 获取相机当前 PayloadSize（帧数据大小）
    unsigned int GetPayloadSize();

    // 打印/查询相机支持的像素格式及帧率范围（调试用）
    void PrintCameraCapabilities();

    // 是否已打开
    bool IsOpen() const
    {
        return m_isOpen;
    }

    // 是否正在采集
    bool IsGrabbing() const
    {
        return m_isGrabbing;
    }

    // 获取最后的错误信息
    std::string GetLastError() const
    {
        return m_lastError;
    }

    // 重连相机（如果断线或采集失败）
    // index: 要重连的设备索引
    // maxRetries: 最大重试次数
    // retryDelayMs: 每次重试间隔（毫秒）
    bool Reconnect(unsigned int index, int maxRetries = 5, int retryDelayMs = 1000);

  private:
    void *m_handle;                 // 相机句柄
    bool m_isOpen;                  // 是否已打开
    bool m_isGrabbing;              // 是否正在采集
    std::string m_lastError;        // 最后的错误信息
    unsigned char *m_convertBuffer; // 图像转换缓冲区
    unsigned int m_bufferSize;      // 缓冲区大小

    // 用于断线重连时恢复状态的缓存值
    unsigned int m_deviceIndex;      // 当前设备索引
    float m_savedExposure;           // 最近设置的曝光时间（微秒）
    float m_savedGain;               // 最近设置的增益（dB）
    bool m_savedTrigger;             // 最近设置的触发模式
    float m_savedFrameRate;          // 最近设置的帧率 (fps)
    unsigned int m_savedPixelFormat; // 最近设置的像素格式

    // 设置错误信息
    void SetError(const std::string &error, int errorCode);

    // 禁止拷贝
    HikCamera(const HikCamera &) = delete;
    HikCamera &operator=(const HikCamera &) = delete;
};

} // namespace hik

#endif // HIK_CAMERA_H
