#include "HikCamera.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

namespace hik
{

// 构造函数
HikCamera::HikCamera()
    : m_handle(nullptr), m_isOpen(false), m_isGrabbing(false), m_convertBuffer(nullptr), m_bufferSize(0),
      m_deviceIndex(0), m_savedExposure(0.0f), m_savedGain(0.0f), m_savedTrigger(false), m_savedFrameRate(0.0f),
      m_savedPixelFormat(0)
{
}

// 设置帧率 (fps)
bool HikCamera::SetFrameRate(float fps)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    // 部分相机使用 AcquisitionFrameRate 设置帧率
    int ret = MV_CC_SetFloatValue(m_handle, "AcquisitionFrameRate", fps);
    if (ret != MV_OK)
    {
        // 有些 SDK 需要先打开帧率控制开关，尝试开启并重试
        MV_CC_SetEnumValue(m_handle, "AcquisitionFrameRateEnable", 1);
        ret = MV_CC_SetFloatValue(m_handle, "AcquisitionFrameRate", fps);
        if (ret != MV_OK)
        {
            SetError("Set frame rate failed", ret);
            return false;
        }
    }

    m_savedFrameRate = fps;
    return true;
}

// 获取帧率
float HikCamera::GetFrameRate()
{
    if (!m_isOpen)
    {
        return 0.0f;
    }

    MVCC_FLOATVALUE value;
    int ret = MV_CC_GetFloatValue(m_handle, "AcquisitionFrameRate", &value);
    if (ret != MV_OK)
    {
        return 0.0f;
    }
    return value.fCurValue;
}

// 设置像素格式（通过像素格式常量）
bool HikCamera::SetPixelFormat(unsigned int pixelFormat)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetEnumValue(m_handle, "PixelFormat", pixelFormat);
    if (ret != MV_OK)
    {
        SetError("Set pixel format failed", ret);
        return false;
    }

    m_savedPixelFormat = pixelFormat;
    return true;
}

// 获取像素格式
unsigned int HikCamera::GetPixelFormat()
{
    if (!m_isOpen)
    {
        return 0;
    }

    MVCC_ENUMVALUE value;
    int ret = MV_CC_GetEnumValue(m_handle, "PixelFormat", &value);
    if (ret != MV_OK)
    {
        return 0;
    }

    return value.nCurValue;
}

// 析构函数
HikCamera::~HikCamera()
{
    if (m_isGrabbing)
    {
        StopGrabbing();
    }
    if (m_isOpen)
    {
        Close();
    }
    if (m_convertBuffer)
    {
        delete[] m_convertBuffer;
        m_convertBuffer = nullptr;
    }
}

// 设置错误信息
void HikCamera::SetError(const std::string &error, int errorCode)
{
    std::ostringstream oss;
    oss << error << " (Error code: 0x" << std::hex << errorCode << ")";
    m_lastError = oss.str();
    std::cerr << "HikCamera Error: " << m_lastError << std::endl;
}

// 枚举所有可用相机
std::vector<CameraInfo> HikCamera::EnumerateDevices()
{
    std::vector<CameraInfo> devices;

    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    // 枚举设备
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK)
    {
        std::cerr << "Enumerate devices failed! Error code: 0x" << std::hex << ret << std::endl;
        return devices;
    }

    std::cout << "Found " << deviceList.nDeviceNum << " device(s)" << std::endl;

    // 遍历设备列表
    for (unsigned int i = 0; i < deviceList.nDeviceNum; i++)
    {
        MV_CC_DEVICE_INFO *pDeviceInfo = deviceList.pDeviceInfo[i];
        if (!pDeviceInfo)
            continue;

        CameraInfo info;
        info.deviceType = pDeviceInfo->nTLayerType;

        if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE)
        {
            // GigE 相机
            MV_GIGE_DEVICE_INFO *pGigEInfo = &pDeviceInfo->SpecialInfo.stGigEInfo;

            info.serialNumber = std::string((char *)pGigEInfo->chSerialNumber);
            info.modelName = std::string((char *)pGigEInfo->chModelName);

            // 格式化 IP 地址
            unsigned int ip = pGigEInfo->nCurrentIp;
            std::ostringstream oss;
            oss << ((ip & 0xFF000000) >> 24) << "." << ((ip & 0x00FF0000) >> 16) << "." << ((ip & 0x0000FF00) >> 8)
                << "." << (ip & 0x000000FF);
            info.ipAddress = oss.str();

            std::cout << "[" << i << "] GigE Camera:" << std::endl;
            std::cout << "    Model: " << info.modelName << std::endl;
            std::cout << "    Serial: " << info.serialNumber << std::endl;
            std::cout << "    IP: " << info.ipAddress << std::endl;
        }
        else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE)
        {
            // USB 相机
            MV_USB3_DEVICE_INFO *pUSBInfo = &pDeviceInfo->SpecialInfo.stUsb3VInfo;

            info.serialNumber = std::string((char *)pUSBInfo->chSerialNumber);
            info.modelName = std::string((char *)pUSBInfo->chModelName);
            info.ipAddress = "USB";

            std::cout << "[" << i << "] USB Camera:" << std::endl;
            std::cout << "    Model: " << info.modelName << std::endl;
            std::cout << "    Serial: " << info.serialNumber << std::endl;
        }
        std::cout << "founded" << std::endl;
        devices.push_back(info);
    }

    return devices;
}

// 打开相机 (通过索引)
bool HikCamera::Open(unsigned int index)
{
    if (m_isOpen)
    {
        m_lastError = "Camera is already open";
        return false;
    }

    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK)
    {
        SetError("Enumerate devices failed", ret);
        return false;
    }

    if (index >= deviceList.nDeviceNum)
    {
        m_lastError = "Invalid device index";
        return false;
    }

    // 创建句柄
    ret = MV_CC_CreateHandle(&m_handle, deviceList.pDeviceInfo[index]);
    if (ret != MV_OK)
    {
        SetError("Create handle failed", ret);
        return false;
    }

    // 打开设备
    ret = MV_CC_OpenDevice(m_handle);
    if (ret != MV_OK)
    {
        SetError("Open device failed", ret);
        MV_CC_DestroyHandle(m_handle);
        m_handle = nullptr;
        return false;
    }

    // 设置触发模式为关闭（连续采集）
    ret = MV_CC_SetEnumValue(m_handle, "TriggerMode", 0);
    if (ret != MV_OK)
    {
        std::cerr << "Warning: Set trigger mode failed, error code: 0x" << std::hex << ret << std::endl;
    }

    m_isOpen = true;
    std::cout << "Camera opened successfully (index: " << index << ")" << std::endl;
    return true;
}

// 打开相机 (通过序列号)
bool HikCamera::OpenBySerialNumber(const std::string &serialNumber)
{
    if (m_isOpen)
    {
        m_lastError = "Camera is already open";
        return false;
    }

    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK)
    {
        SetError("Enumerate devices failed", ret);
        return false;
    }

    // 查找指定序列号的设备
    int deviceIndex = -1;
    for (unsigned int i = 0; i < deviceList.nDeviceNum; i++)
    {
        MV_CC_DEVICE_INFO *pDeviceInfo = deviceList.pDeviceInfo[i];
        std::string sn;

        if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE)
        {
            sn = std::string((char *)pDeviceInfo->SpecialInfo.stGigEInfo.chSerialNumber);
        }
        else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE)
        {
            sn = std::string((char *)pDeviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
        }

        if (sn == serialNumber)
        {
            deviceIndex = i;
            break;
        }
    }

    if (deviceIndex < 0)
    {
        m_lastError = "Device with serial number '" + serialNumber + "' not found";
        return false;
    }

    return Open(deviceIndex);
}

// 关闭相机
bool HikCamera::Close()
{
    if (!m_isOpen)
    {
        return true;
    }

    if (m_isGrabbing)
    {
        StopGrabbing();
    }

    // 关闭设备
    int ret = MV_CC_CloseDevice(m_handle);
    if (ret != MV_OK)
    {
        SetError("Close device failed", ret);
    }

    // 销毁句柄
    ret = MV_CC_DestroyHandle(m_handle);
    if (ret != MV_OK)
    {
        SetError("Destroy handle failed", ret);
    }

    m_handle = nullptr;
    m_isOpen = false;
    std::cout << "Camera closed" << std::endl;
    return true;
}

// 开始采集
bool HikCamera::StartGrabbing()
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    if (m_isGrabbing)
    {
        return true;
    }

    int ret = MV_CC_StartGrabbing(m_handle);
    if (ret != MV_OK)
    {
        SetError("Start grabbing failed", ret);
        return false;
    }

    m_isGrabbing = true;
    std::cout << "Start grabbing" << std::endl;
    return true;
}

// 停止采集
bool HikCamera::StopGrabbing()
{
    if (!m_isGrabbing)
    {
        return true;
    }

    int ret = MV_CC_StopGrabbing(m_handle);
    if (ret != MV_OK)
    {
        SetError("Stop grabbing failed", ret);
        return false;
    }

    m_isGrabbing = false;
    std::cout << "Stop grabbing" << std::endl;
    return true;
}

// 获取一帧图像
bool HikCamera::GrabImage(ImageData &imageData, unsigned int timeout)
{
    if (!m_isGrabbing)
    {
        m_lastError = "Camera is not grabbing";
        return false;
    }

    MV_FRAME_OUT frameInfo;
    memset(&frameInfo, 0, sizeof(MV_FRAME_OUT));

    int ret = MV_CC_GetImageBuffer(m_handle, &frameInfo, timeout);
    if (ret != MV_OK)
    {
        if (ret != MV_E_NODATA)
        {
            SetError("Get image buffer failed", ret);
        }
        return false;
    }

    imageData.width = frameInfo.stFrameInfo.nWidth;
    imageData.height = frameInfo.stFrameInfo.nHeight;
    imageData.pixelFormat = frameInfo.stFrameInfo.enPixelType;
    imageData.dataSize = frameInfo.stFrameInfo.nFrameLen;
    imageData.data = frameInfo.pBufAddr;

    // 注意: 使用完后需要调用 MV_CC_FreeImageBuffer 释放缓冲区
    // 这里为了简化，在下次调用时会自动释放
    MV_CC_FreeImageBuffer(m_handle, &frameInfo);

    return true;
}

// 获取一帧图像并转换为BGR格式
bool HikCamera::GrabImageBGR(ImageData &imageData, unsigned int timeout)
{
    if (!m_isGrabbing)
    {
        m_lastError = "Camera is not grabbing";
        return false;
    }

    MV_FRAME_OUT frameInfo;
    memset(&frameInfo, 0, sizeof(MV_FRAME_OUT));

    int ret = MV_CC_GetImageBuffer(m_handle, &frameInfo, timeout);
    if (ret != MV_OK)
    {
        if (ret != MV_E_NODATA)
        {
            SetError("Get image buffer failed", ret);
        }
        return false;
    }

    // 分配转换缓冲区
    unsigned int nBGRSize = frameInfo.stFrameInfo.nWidth * frameInfo.stFrameInfo.nHeight * 3;
    if (m_bufferSize < nBGRSize)
    {
        if (m_convertBuffer)
        {
            delete[] m_convertBuffer;
        }
        m_convertBuffer = new unsigned char[nBGRSize];
        m_bufferSize = nBGRSize;
    }

    // 判断是否需要转换
    bool needConvert = true;
    if (frameInfo.stFrameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
    {
        // 已经是BGR格式，直接复制
        needConvert = false;
        memcpy(m_convertBuffer, frameInfo.pBufAddr, frameInfo.stFrameInfo.nFrameLen);
    }
    else
    {
        // 转换为 BGR 格式
        MV_CC_PIXEL_CONVERT_PARAM convertParam;
        memset(&convertParam, 0, sizeof(MV_CC_PIXEL_CONVERT_PARAM));
        convertParam.nWidth = frameInfo.stFrameInfo.nWidth;
        convertParam.nHeight = frameInfo.stFrameInfo.nHeight;
        convertParam.pSrcData = frameInfo.pBufAddr;
        convertParam.nSrcDataLen = frameInfo.stFrameInfo.nFrameLen;
        convertParam.enSrcPixelType = frameInfo.stFrameInfo.enPixelType;
        convertParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
        convertParam.pDstBuffer = m_convertBuffer;
        convertParam.nDstBufferSize = nBGRSize;

        ret = MV_CC_ConvertPixelType(m_handle, &convertParam);
        if (ret != MV_OK)
        {
            SetError("Convert pixel type failed", ret);
            MV_CC_FreeImageBuffer(m_handle, &frameInfo);
            return false;
        }
    }

    imageData.width = frameInfo.stFrameInfo.nWidth;
    imageData.height = frameInfo.stFrameInfo.nHeight;
    imageData.pixelFormat = PixelType_Gvsp_BGR8_Packed;
    imageData.dataSize = nBGRSize;
    imageData.data = m_convertBuffer;

    MV_CC_FreeImageBuffer(m_handle, &frameInfo);

    return true;
}

// 设置曝光时间
bool HikCamera::SetExposureTime(float exposureTime)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetFloatValue(m_handle, "ExposureTime", exposureTime);
    if (ret != MV_OK)
    {
        SetError("Set exposure time failed", ret);
        return false;
    }

    // 保存当前设置以便重连后恢复
    m_savedExposure = exposureTime;

    return true;
}

// 获取曝光时间
float HikCamera::GetExposureTime()
{
    if (!m_isOpen)
    {
        return 0.0f;
    }

    MVCC_FLOATVALUE value;
    int ret = MV_CC_GetFloatValue(m_handle, "ExposureTime", &value);
    if (ret != MV_OK)
    {
        return 0.0f;
    }

    return value.fCurValue;
}

// 设置增益
bool HikCamera::SetGain(float gain)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetFloatValue(m_handle, "Gain", gain);
    if (ret != MV_OK)
    {
        SetError("Set gain failed", ret);
        return false;
    }

    // 保存当前设置以便重连后恢复
    m_savedGain = gain;

    return true;
}

// 获取增益
float HikCamera::GetGain()
{
    if (!m_isOpen)
    {
        return 0.0f;
    }

    MVCC_FLOATVALUE value;
    int ret = MV_CC_GetFloatValue(m_handle, "Gain", &value);
    if (ret != MV_OK)
    {
        return 0.0f;
    }

    return value.fCurValue;
}

// 设置触发模式
bool HikCamera::SetTriggerMode(bool enable)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetEnumValue(m_handle, "TriggerMode", enable ? 1 : 0);
    if (ret != MV_OK)
    {
        SetError("Set trigger mode failed", ret);
        return false;
    }

    // 保存当前触发模式以便重连后恢复
    m_savedTrigger = enable;

    return true;
}

// 重连实现
bool HikCamera::Reconnect(unsigned int index, int maxRetries, int retryDelayMs)
{
    // 保存索引
    m_deviceIndex = index;

    for (int attempt = 1; attempt <= maxRetries && m_isOpen == false; ++attempt)
    {
        std::cout << "Attempting reconnect (index=" << index << ") try=" << attempt << "..." << std::endl;

        // 确保先关闭旧的资源
        if (m_isGrabbing)
        {
            StopGrabbing();
        }
        if (m_isOpen)
        {
            Close();
        }

        // 退避等待
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs * attempt));

        // 重新打开
        if (!Open(index))
        {
            std::cout << "Reconnect open failed: " << GetLastError() << std::endl;
            continue;
        }

        // 恢复设置
        if (m_savedExposure > 0.0f)
        {
            SetExposureTime(m_savedExposure);
        }
        if (m_savedGain > 0.0f)
        {
            SetGain(m_savedGain);
        }
        if (m_savedFrameRate > 0.0f)
        {
            SetFrameRate(m_savedFrameRate);
        }
        if (m_savedPixelFormat > 0)
        {
            SetPixelFormat(m_savedPixelFormat);
        }
        SetTriggerMode(m_savedTrigger);

        // 启动采集
        if (!StartGrabbing())
        {
            std::cout << "Reconnect start grabbing failed: " << GetLastError() << std::endl;
            Close();
            continue;
        }

        std::cout << "Reconnect successful" << std::endl;
        return true;
    }

    std::cout << "Reconnect failed after " << maxRetries << " attempts" << std::endl;
    return false;
}

// 软件触发一次
bool HikCamera::TriggerSoftware()
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetCommandValue(m_handle, "TriggerSoftware");
    if (ret != MV_OK)
    {
        SetError("Software trigger failed", ret);
        return false;
    }

    return true;
}

// 获取图像宽度
unsigned int HikCamera::GetWidth()
{
    if (!m_isOpen)
    {
        return 0;
    }

    MVCC_INTVALUE value;
    int ret = MV_CC_GetIntValue(m_handle, "Width", &value);
    if (ret != MV_OK)
    {
        return 0;
    }

    return value.nCurValue;
}

// 获取图像高度
unsigned int HikCamera::GetHeight()
{
    if (!m_isOpen)
    {
        return 0;
    }

    MVCC_INTVALUE value;
    int ret = MV_CC_GetIntValue(m_handle, "Height", &value);
    if (ret != MV_OK)
    {
        return 0;
    }

    return value.nCurValue;
}

// 设置 PacketSize（字节），通常用于 GigE: GevSCPSPacketSize
bool HikCamera::SetPacketSize(unsigned int packetSize)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetIntValue(m_handle, "GevSCPSPacketSize", packetSize);
    if (ret != MV_OK)
    {
        // 有些相机使用不同的名字或不支持此接口
        SetError("Set PacketSize failed", ret);
        return false;
    }

    return true;
}

unsigned int HikCamera::GetPacketSize()
{
    if (!m_isOpen)
        return 0;

    MVCC_INTVALUE val;
    int ret = MV_CC_GetIntValue(m_handle, "GevSCPSPacketSize", &val);
    if (ret != MV_OK)
        return 0;
    return val.nCurValue;
}

// 设置 PacketDelay（微秒），通常用于 GigE: GevSCPD
bool HikCamera::SetPacketDelay(unsigned int packetDelay)
{
    if (!m_isOpen)
    {
        m_lastError = "Camera is not open";
        return false;
    }

    int ret = MV_CC_SetIntValue(m_handle, "GevSCPD", packetDelay);
    if (ret != MV_OK)
    {
        SetError("Set PacketDelay failed", ret);
        return false;
    }

    return true;
}

unsigned int HikCamera::GetPacketDelay()
{
    if (!m_isOpen)
        return 0;

    MVCC_INTVALUE val;
    int ret = MV_CC_GetIntValue(m_handle, "GevSCPD", &val);
    if (ret != MV_OK)
        return 0;
    return val.nCurValue;
}

// 获取相机实际输出/结果帧率（ResultingFrameRate）
float HikCamera::GetResultingFrameRate()
{
    if (!m_isOpen)
        return 0.0f;

    MVCC_FLOATVALUE val;
    int ret = MV_CC_GetFloatValue(m_handle, "ResultingFrameRate", &val);
    if (ret != MV_OK)
        return 0.0f;
    return val.fCurValue;
}

// 获取 PayloadSize
unsigned int HikCamera::GetPayloadSize()
{
    if (!m_isOpen)
        return 0;

    MVCC_INTVALUE val;
    int ret = MV_CC_GetIntValue(m_handle, "PayloadSize", &val);
    if (ret != MV_OK)
        return 0;
    return val.nCurValue;
}

// 打印相机能力（可支持的像素格式/帧率区间等），用于调试
void HikCamera::PrintCameraCapabilities()
{
    if (!m_isOpen)
    {
        std::cout << "Camera is not open, cannot print capabilities" << std::endl;
        return;
    }

    // 打印当前像素格式
    unsigned int pf = GetPixelFormat();
    std::cout << "Current PixelFormat: 0x" << std::hex << pf << std::dec << std::endl;

    // 打印帧率范围（若可用）
    MVCC_FLOATVALUE frVal;
    if (MV_CC_GetFloatValue(m_handle, "AcquisitionFrameRate", &frVal) == MV_OK)
    {
        std::cout << "AcquisitionFrameRate: " << frVal.fCurValue << " (min:" << frVal.fMin << " max:" << frVal.fMax
                  << ")" << std::endl;
    }

    // 打印 PayloadSize
    std::cout << "PayloadSize: " << GetPayloadSize() << std::endl;

    // 打印 ResultingFrameRate
    std::cout << "ResultingFrameRate: " << GetResultingFrameRate() << std::endl;

    // 打印 PacketSize / PacketDelay（若支持）
    std::cout << "PacketSize: " << GetPacketSize() << " PacketDelay: " << GetPacketDelay() << std::endl;
}

} // namespace hik
