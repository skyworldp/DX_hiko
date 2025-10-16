#include "ArmorMatcher.h"
#include <vector>
#include <fstream>
#include <mutex>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace armor
{
namespace
{
std::shared_ptr<ArmorMatcher> g_matcher;
std::mutex g_matcherMutex;
} // namespace

bool ArmorMatcher::load(const std::string &modelPath, int inputWidth, int inputHeight)
{
    ready_ = false;
    labels_.clear();
    lastError_.clear();
    modelPath_ = modelPath;
    // 强制使用 224x224
    inputWidth_ = 224;
    inputHeight_ = 224;
    try
    {
        net_ = cv::dnn::readNetFromONNX(modelPath);
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
    catch (const cv::Exception &e)
    {
        lastError_ = std::string("加载 ONNX 模型失败: ") + e.what();
        return false;
    }

    ready_ = true;
    return true;
}

bool ArmorMatcher::loadWithLabels(const std::string &modelPath, const std::string &labelsPath, int inputWidth,
                                  int inputHeight)
{
    // 先调用基本的 load 以加载模型
    if (!load(modelPath, inputWidth, inputHeight))
        return false;

    labels_.clear();
    if (!labelsPath.empty())
    {
        std::ifstream ifs(labelsPath);
        if (!ifs.is_open())
        {
            lastError_ = std::string("无法打开 labels 文件: ") + labelsPath;
            // labels 失败不认为是致命错误，模型仍可工作，只是没有标签映射
            return true;
        }
        std::string line;
        while (std::getline(ifs, line))
        {
            if (!line.empty())
                labels_.push_back(line);
        }
    }

    return true;
}

MatchResult ArmorMatcher::match(const cv::Mat &image) const
{
    MatchResult result;
    if (!ready_)
    {
        result.error = "模型尚未加载";
        lastError_ = result.error;
        return result;
    }

    if (image.empty())
    {
        result.error = "输入图像为空";
        lastError_ = result.error;
        return result;
    }

    // 先将输入转换为灰度并二值化（阈值 100），然后再转换为三通道用于网络
    cv::Mat gray;
    if (image.channels() == 1)
    {
        gray = image.clone();
    }
    else if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else if (image.channels() == 4)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    }
    else
    {
        result.error = "不支持的通道数: " + std::to_string(image.channels());
        lastError_ = result.error;
        return result;
    }

    // 先做严格二值化（纯黑白），阈值使用 100
    cv::Mat bwStrict;
    cv::threshold(gray, bwStrict, 40, 255, cv::THRESH_BINARY);
    imshow("Binary Strict", bwStrict);
    // 将单通道二值图复制为三通道 BGR（每通道相同）
    cv::Mat bgr;

    cv::cvtColor(bwStrict, bgr, cv::COLOR_GRAY2BGR);
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(inputWidth_, inputHeight_), cv::INTER_LINEAR);
    if (resized.cols >= 150)
    {
        int targetW = 150;
        int targetH = resized.rows;
        int x = (resized.cols - targetW) / 2;
        int y = 0;
        cv::Rect roi(x, y, targetW, targetH);
        resized = resized(roi).clone();
    }

    // 转为浮点并缩放到 [0,1]
    resized.convertTo(resized, CV_32F, 1.0f / 255.0f);
    cv::imshow("Resized", resized);
    
    cv::Mat mean = (cv::Mat_<float>(1, 3) << 0.485f, 0.456f, 0.406f);
    cv::Mat stdv = (cv::Mat_<float>(1, 3) << 0.229f, 0.224f, 0.225f);
    std::vector<cv::Mat> chans(3);
    cv::split(resized, chans);
    for (int i = 0; i < 3; i++)
        chans[i] = (chans[i] - mean.at<float>(i)) / stdv.at<float>(i);
    cv::merge(chans, resized);
    // 生成 1x3xH xW 的 blob（值为 0 或 1）
    cv::Mat blob = cv::dnn::blobFromImage(resized);
    
    cv::Mat out;
    try
    {
        net_.setInput(blob);
        out = net_.forward();
    }
    catch (const cv::Exception &e)
    {
        result.error = std::string("推理失败: ") + e.what();
        lastError_ = result.error;
        return result;
    }

    cv::Point classIdPoint;
    double confidence = 0.0;
    cv::minMaxLoc(out, nullptr, &confidence, nullptr, &classIdPoint);
    int classId = classIdPoint.x;
    // 如果 labels_ 可用，则映射为标签文本，否则使用索引字符串
    std::string label;
    if (!labels_.empty() && classId >= 0 && classId < (int)labels_.size())
        label = labels_[classId];
    else
        label = std::to_string(classId);

    result.success = true;
    result.classId = classId;
    result.confidence = confidence;
    result.label = label;
    return result;
}

void setGlobalArmorMatcher(const std::shared_ptr<ArmorMatcher> &matcher)
{
    std::lock_guard<std::mutex> lock(g_matcherMutex);
    g_matcher = matcher;
}

std::shared_ptr<ArmorMatcher> getGlobalArmorMatcher()
{
    std::lock_guard<std::mutex> lock(g_matcherMutex);
    return g_matcher;
}

} // namespace armor
