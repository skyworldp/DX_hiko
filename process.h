#pragma once

#include <opencv2/opencv.hpp>

// 处理接口：接收 BGR 彩色图像，输出二值图和带标注结果图
// frame: 输入 BGR 彩色图（将被只读访问）
// binaryOut: 输出单通道二值图（CV_8UC1）
// result: 输出带标注的彩色图（CV_8UC3）
void processFrame(cv::Mat &frame, cv::Mat &binaryOut, cv::Mat &result);
