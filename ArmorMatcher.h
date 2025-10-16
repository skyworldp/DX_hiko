#pragma once

#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>

namespace armor
{

struct MatchResult
{
    bool success = false;
    int classId = -1;
    double confidence = 0.0;
    std::string label;
    std::string error;
};

class ArmorMatcher
{
  public:
    ArmorMatcher() = default;

    /**
     * @brief 加载 ONNX 模型
     * @param modelPath 模型文件路径
     * @param inputSize 网络输入尺寸，固定为 224x224
     * @return 是否加载成功
     */
    bool load(const std::string &modelPath, int inputWidth = 224, int inputHeight = 224);

    /**
     * @brief 加载 ONNX 模型并同时从 labels 文件加载标签（可选）
     * @param modelPath 模型文件路径
     * @param labelsPath 每行一个标签的文本文件路径（如果不存在则忽略）
     * @param inputWidth 网络输入宽度，默认224
     * @param inputHeight 网络输入高度，默认224
     * @return 是否加载成功（模型加载失败则返回 false；labels 无法打开不会使得方法失败，但会记录 lastError）
     */
    bool loadWithLabels(const std::string &modelPath, const std::string &labelsPath, int inputWidth = 224,
                        int inputHeight = 224);

    /**
     * @brief 匹配装甲板图像，返回分类结果
     * @param image BGR 或 RGB 图像（将自动转换为网络输入格式）
     */
    MatchResult match(const cv::Mat &image) const;

    /**
     * @brief 判断模型是否已经成功加载
     */
    bool isReady() const noexcept
    {
        return ready_;
    }

    /**
     * @brief 获取最近一次错误信息
     */
    const std::string &lastError() const noexcept
    {
        return lastError_;
    }

  private:
    bool ready_ = false;
    int inputWidth_ = 224;
    int inputHeight_ = 224;
    std::vector<std::string> labels_; // 可选：保留但不强制加载
    mutable std::string lastError_;
    std::string modelPath_;
    mutable cv::dnn::Net net_;
};

void setGlobalArmorMatcher(const std::shared_ptr<ArmorMatcher> &matcher);
std::shared_ptr<ArmorMatcher> getGlobalArmorMatcher();

} // namespace armor
