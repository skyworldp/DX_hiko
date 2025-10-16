#include "process.h"
#include "ArmorMatcher.h"
#include "opencv2/opencv.hpp" // IWYU pragma: keep
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
/*opencv.hpp包含了Opencv各模块的头文件，如高层GUI图形用户界面模块头文件highgui.hpp，图像处理模块头文件imgprogc.hpp等。所以，用"#include<opencv/opencv.hpp>"即可，达到精简代码的作用。*/
using namespace std;
using namespace cv;

// ============ 相机标定参数（需要根据你的相机进行标定后填入）============
// 相机内参矩阵
Mat cameraMatrix = (Mat_<double>(3, 3) << 2.8496, 0, 1.5937, // fx, 0, cx
                    0, 2.8402, 1.0963,                       // 0, fy, cy
                    0, 0, 0.0010);                           // 0, 0, 10
// 畸变系数 [k1, k2, p1, p2, k3]
Mat distCoeffs = (Mat_<double>(5, 1) << 0, 0, 0, 0, 0);

// ============ 灯条物理尺寸（单位：mm，需要根据实际测量填入）============
const double LIGHT_BAR_WIDTH = 10.0;  // 灯条宽度 (mm)
const double LIGHT_BAR_HEIGHT = 50.0; // 灯条高度/长度 (mm)
const double ARMOR_WIDTH = 135.0;     // 两个灯条中心之间的距离 (mm)

// PnP 解算结果结构体
struct PnPResult
{
    bool success;
    Mat rvec;        // 旋转向量
    Mat tvec;        // 平移向量
    double distance; // 距离
};

// PnP 解算函数
PnPResult solvePnPForArmor(const RotatedRect &leftBar, const RotatedRect &rightBar)
{
    PnPResult result;
    result.success = false;

    // 定义装甲板的 3D 坐标（物理坐标系，单位 mm）
    vector<Point3f> objectPoints;

    float halfArmorWidth = ARMOR_WIDTH / 2.0;
    float halfBarHeight = LIGHT_BAR_HEIGHT / 2.0;
    float halfBarWidth = LIGHT_BAR_WIDTH / 2.0;

    // 左灯条四个角点（逆时针顺序）
    objectPoints.push_back(Point3f(-halfArmorWidth - halfBarWidth, halfBarHeight, 0));
    objectPoints.push_back(Point3f(-halfArmorWidth + halfBarWidth, halfBarHeight, 0));
    objectPoints.push_back(Point3f(-halfArmorWidth + halfBarWidth, -halfBarHeight, 0));
    objectPoints.push_back(Point3f(-halfArmorWidth - halfBarWidth, -halfBarHeight, 0));

    // 右灯条四个角点
    objectPoints.push_back(Point3f(halfArmorWidth - halfBarWidth, halfBarHeight, 0));
    objectPoints.push_back(Point3f(halfArmorWidth + halfBarWidth, halfBarHeight, 0));
    objectPoints.push_back(Point3f(halfArmorWidth + halfBarWidth, -halfBarHeight, 0));
    objectPoints.push_back(Point3f(halfArmorWidth - halfBarWidth, -halfBarHeight, 0));

    // 获取图像点
    vector<Point2f> imagePoints;
    Point2f leftVertices[4], rightVertices[4];
    leftBar.points(leftVertices);
    rightBar.points(rightVertices);

    // 添加左右灯条的角点
    for (int k = 0; k < 4; k++)
    {
        imagePoints.push_back(leftVertices[k]);
    }
    for (int k = 0; k < 4; k++)
    {
        imagePoints.push_back(rightVertices[k]);
    }

    // 执行 PnP 解算
    result.success = solvePnP(objectPoints, imagePoints, cameraMatrix, distCoeffs, result.rvec, result.tvec, false,
                              SOLVEPNP_ITERATIVE);

    if (result.success)
    {
        // 计算距离
        result.distance = sqrt(result.tvec.at<double>(0) * result.tvec.at<double>(0) +
                               result.tvec.at<double>(1) * result.tvec.at<double>(1) +
                               result.tvec.at<double>(2) * result.tvec.at<double>(2));
    }

    return result;
}

// 灯条结构（用直线表示）
struct LightBar
{
    Vec4f line;        // 拟合直线 [vx, vy, x0, y0]
    Point2f center;    // 中心点
    Point2f endpoint1; // 端点1
    Point2f endpoint2; // 端点2
    float length;      // 长度
    float angle;       // 角度（度）
};

// 透视变换：将装甲板区域变换为正面视图（沿灯条方向延伸）
Mat warpArmorToFrontView(const Mat &frame, const LightBar &leftBar, const LightBar &rightBar)
{
    // 直接使用拟合直线的端点
    Point2f leftEnd1 = leftBar.endpoint1;
    Point2f leftEnd2 = leftBar.endpoint2;
    Point2f rightEnd1 = rightBar.endpoint1;
    Point2f rightEnd2 = rightBar.endpoint2;

    // 计算延伸距离（灯条长度的 1/2）
    float leftExtension = leftBar.length / 1.5f;
    float rightExtension = rightBar.length / 1.5f;

    // 计算灯条的方向向量（从端点1到端点2）
    Point2f leftDir = leftEnd2 - leftEnd1;
    float leftDirLength = sqrt(leftDir.x * leftDir.x + leftDir.y * leftDir.y);
    if (leftDirLength > 0)
    {
        leftDir = leftDir / leftDirLength; // 归一化
    }
    else
    {
        return Mat();
    }

    Point2f rightDir = rightEnd2 - rightEnd1;
    float rightDirLength = sqrt(rightDir.x * rightDir.x + rightDir.y * rightDir.y);
    if (rightDirLength > 0)
    {
        rightDir = rightDir / rightDirLength; // 归一化
    }
    else
    {
        return Mat();
    }

    // 沿着灯条方向向两端延伸
    Point2f extendedLeft1 = leftEnd1 - leftDir * leftExtension;
    Point2f extendedLeft2 = leftEnd2 + leftDir * leftExtension;
    Point2f extendedRight1 = rightEnd1 - rightDir * rightExtension;
    Point2f extendedRight2 = rightEnd2 + rightDir * rightExtension;

    // 确定上下顺序（Y坐标小的在上）
    Point2f leftTop, leftBottom, rightTop, rightBottom;
    if (extendedLeft1.y < extendedLeft2.y)
    {
        leftTop = extendedLeft1;
        leftBottom = extendedLeft2;
    }
    else
    {
        leftTop = extendedLeft2;
        leftBottom = extendedLeft1;
    }

    if (extendedRight1.y < extendedRight2.y)
    {
        rightTop = extendedRight1;
        rightBottom = extendedRight2;
    }
    else
    {
        rightTop = extendedRight2;
        rightBottom = extendedRight1;
    }

    // 装甲板四个角点（源点）- 按左上、右上、右下、左下顺序
    vector<Point2f> srcPoints;
    srcPoints.push_back(leftTop);
    srcPoints.push_back(rightTop);
    srcPoints.push_back(rightBottom);
    srcPoints.push_back(leftBottom);

    // ========== 健壮性检查 ==========
    // 1. 检查四个点是否在图像范围内
    for (const auto &pt : srcPoints)
    {
        if (pt.x < 0 || pt.x >= frame.cols || pt.y < 0 || pt.y >= frame.rows)
        {
            // 如果有点超出图像范围，返回空图像
            return Mat();
        }
    }

    // 2. 检查四个点是否构成合法的四边形（不能共线）
    // 计算面积，如果面积太小说明接近共线
    float area = contourArea(srcPoints);
    if (area < 100.0f)
    {
        return Mat();
    }

    // 3. 检查四边形是否为凸多边形
    if (!isContourConvex(srcPoints))
    {
        return Mat();
    }

    // 目标矩形尺寸（正面视图）
    int dstWidth = 224;
    int dstHeight = 224;
    vector<Point2f> dstPoints;
    dstPoints.push_back(Point2f(0, 0));
    dstPoints.push_back(Point2f(dstWidth - 1, 0));
    dstPoints.push_back(Point2f(dstWidth - 1, dstHeight - 1));
    dstPoints.push_back(Point2f(0, dstHeight - 1));

    // 计算透视变换矩阵
    Mat perspectiveMatrix;
    try
    {
        perspectiveMatrix = getPerspectiveTransform(srcPoints, dstPoints);
    }
    catch (const cv::Exception &e)
    {
        // 如果透视变换矩阵计算失败，返回空图像
        cerr << "透视变换矩阵计算失败: " << e.what() << endl;
        return Mat();
    }

    // 执行透视变换
    Mat warpedArmor;
    try
    {
        warpPerspective(frame, warpedArmor, perspectiveMatrix, Size(dstWidth, dstHeight));
    }
    catch (const cv::Exception &e)
    {
        cerr << "透视变换执行失败: " << e.what() << endl;
        return Mat();
    }

    // 将变换后的图像左右各 50 像素裁剪掉（当图像宽度不足 100 时，返回空图像表示失败）
    if (warpedArmor.empty())
        return Mat();

    int w = warpedArmor.cols;
    int h = warpedArmor.rows;
    const int side = 50;
    if (w <= 2 * side)
    {
        // 宽度不足时，返回空图像以示裁剪失败
        return Mat();
    }

    // 裁剪中间区域
    Rect midRoi(side, 0, w - 2 * side, h);
    Mat cropped = warpedArmor(midRoi).clone();

    return cropped;
}

void processFrame(Mat &frame, Mat &binaryOut, Mat &result)
{
    Mat gray, blurred;

    // 转为灰度图
    cvtColor(frame, gray, COLOR_BGR2GRAY);

    // 高斯模糊，去除噪声
    GaussianBlur(gray, blurred, Size(5, 5), 0);

    // 使用亮度阈值检测灯条（不区分颜色）
    Mat binary;
    threshold(blurred, binary, 190, 255, THRESH_BINARY);

    // ============ 增强的形态学操作：连接断裂灯条 ============
    // 1. 大尺寸竖向闭运算：连接竖向断裂的灯条
    Mat verticalKernel = getStructuringElement(MORPH_RECT, Size(1, 7)); // 竖向 1x7
    morphologyEx(binary, binary, MORPH_CLOSE, verticalKernel);

    // 2. 小尺寸矩形闭运算：填充小孔
    Mat smallKernel = getStructuringElement(MORPH_RECT, Size(3, 3));
    morphologyEx(binary, binary, MORPH_CLOSE, smallKernel);

    // 3. 开运算去噪（使用更小的 kernel 避免过度腐蚀）
    Mat openKernel = getStructuringElement(MORPH_RECT, Size(2, 2));
    morphologyEx(binary, binary, MORPH_OPEN, openKernel);

    // 输出二值图
    binaryOut = binary;

    // 查找轮廓
    vector<vector<Point>> contours;
    findContours(binaryOut, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    // 复制原始帧到结果图像
    result = frame.clone();

    // 存储候选灯条（用直线表示）
    vector<LightBar> candidates;
    for (const auto &contour : contours)
    {
        double area = contourArea(contour);

        // 使用最小外接矩形进行初步筛选
        RotatedRect rotRect = minAreaRect(contour);
        double width = rotRect.size.width;
        double height = rotRect.size.height;
        double aspectRatio = max(width, height) / min(width, height);

        // 筛选条件：面积、长宽比（细长的灯条）
        if (area > 50.0 && area < 5000.0 && aspectRatio > 2.5 && contour.size() >= 5)
        {
            // 使用轮廓点拟合直线
            Vec4f fittedLine;
            fitLine(contour, fittedLine, DIST_L2, 0, 0.01, 0.01);

            // fittedLine: [vx, vy, x0, y0]
            // (vx, vy) 是方向向量，(x0, y0) 是直线上的一点
            float vx = fittedLine[0];
            float vy = fittedLine[1];
            float x0 = fittedLine[2];
            float y0 = fittedLine[3];

            // 计算轮廓点在直线上的投影，找到端点
            float minProj = FLT_MAX;
            float maxProj = -FLT_MAX;

            for (const auto &pt : contour)
            {
                // 点到直线上的投影距离（标量投影）
                float proj = (pt.x - x0) * vx + (pt.y - y0) * vy;
                minProj = min(minProj, proj);
                maxProj = max(maxProj, proj);
            }

            // 计算两个端点
            Point2f endpoint1(x0 + minProj * vx, y0 + minProj * vy);
            Point2f endpoint2(x0 + maxProj * vx, y0 + maxProj * vy);

            // 计算中心点和长度
            Point2f center = (endpoint1 + endpoint2) * 0.5f;
            float length = sqrt(pow(endpoint2.x - endpoint1.x, 2) + pow(endpoint2.y - endpoint1.y, 2));

            // 计算角度（相对于水平方向）
            float angle = atan2(vy, vx) * 180.0 / CV_PI;

            LightBar bar;
            bar.line = fittedLine;
            bar.center = center;
            bar.endpoint1 = endpoint1;
            bar.endpoint2 = endpoint2;
            bar.length = length;
            bar.angle = angle;
            candidates.push_back(bar);

            // 绘制拟合的直线（红色）
            line(result, endpoint1, endpoint2, Scalar(0, 0, 255), 2);
            circle(result, center, 3, Scalar(0, 0, 255), -1);
        }
    }

    // 查找并绘制匹配的灯条对（绿色加粗）
    for (int i = 0; i < candidates.size(); ++i)
    {
        for (int j = i + 1; j < candidates.size(); ++j)
        {
            LightBar &bar1 = candidates[i];
            LightBar &bar2 = candidates[j];

            // 计算角度差（判断两个灯条是否平行）
            double angleDiff = abs(bar1.angle - bar2.angle);
            if (angleDiff > 180)
                angleDiff = 360 - angleDiff;

            // 匹配条件：角度差小于4度
            if (angleDiff < 6.0)
            {

                // 绘制匹配的灯条（绿色加粗）
                line(result, bar1.endpoint1, bar1.endpoint2, Scalar(0, 255, 0), 3);
                line(result, bar2.endpoint1, bar2.endpoint2, Scalar(0, 255, 0), 3);

                // 判断哪个灯条在左侧
                bool bar1IsLeft = bar1.center.x < bar2.center.x;
                LightBar &leftBar = bar1IsLeft ? bar1 : bar2;
                LightBar &rightBar = bar1IsLeft ? bar2 : bar1;

                // ============ 计算装甲板四个角点（用于 PnP 解算）============
                // 使用与透视变换相同的逻辑来获取装甲板角点
                Point2f leftEnd1 = leftBar.endpoint1;
                Point2f leftEnd2 = leftBar.endpoint2;
                Point2f rightEnd1 = rightBar.endpoint1;
                Point2f rightEnd2 = rightBar.endpoint2;

                // 计算延伸距离（灯条长度的 1/2）
                float leftExtension = leftBar.length / 1.5f;
                float rightExtension = rightBar.length / 1.5f;

                // 计算灯条的方向向量
                Point2f leftDir = leftEnd2 - leftEnd1;
                float leftDirLength = sqrt(leftDir.x * leftDir.x + leftDir.y * leftDir.y);
                if (leftDirLength > 0)
                    leftDir = leftDir / leftDirLength;
                else
                    continue;

                Point2f rightDir = rightEnd2 - rightEnd1;
                float rightDirLength = sqrt(rightDir.x * rightDir.x + rightDir.y * rightDir.y);
                if (rightDirLength > 0)
                    rightDir = rightDir / rightDirLength;
                else
                    continue;

                // 沿着灯条方向向两端延伸
                Point2f extendedLeft1 = leftEnd1 - leftDir * leftExtension;
                Point2f extendedLeft2 = leftEnd2 + leftDir * leftExtension;
                Point2f extendedRight1 = rightEnd1 - rightDir * rightExtension;
                Point2f extendedRight2 = rightEnd2 + rightDir * rightExtension;

                // 确定上下顺序（Y坐标小的在上）
                Point2f leftTop, leftBottom, rightTop, rightBottom;
                if (extendedLeft1.y < extendedLeft2.y)
                {
                    leftTop = extendedLeft1;
                    leftBottom = extendedLeft2;
                }
                else
                {
                    leftTop = extendedLeft2;
                    leftBottom = extendedLeft1;
                }

                if (extendedRight1.y < extendedRight2.y)
                {
                    rightTop = extendedRight1;
                    rightBottom = extendedRight2;
                }
                else
                {
                    rightTop = extendedRight2;
                    rightBottom = extendedRight1;
                }

                // 装甲板四个角点（左上、右上、右下、左下）
                vector<Point2f> armorCorners;
                armorCorners.push_back(leftTop);
                armorCorners.push_back(rightTop);
                armorCorners.push_back(rightBottom);
                armorCorners.push_back(leftBottom);

                // ============ PnP 解算（使用装甲板四角点）============
                // 定义装甲板的 3D 坐标（物理坐标系，单位 mm）
                vector<Point3f> objectPoints;
                float halfArmorWidth = ARMOR_WIDTH / 2.0;
                float halfBarHeight = LIGHT_BAR_HEIGHT / 2.0;
                // 装甲板延伸后的高度（灯条高度 + 延伸）
                float armorHalfHeight = halfBarHeight * 1.5f; // 延伸了 0.5 倍

                // 装甲板四个角点（按左上、右上、右下、左下顺序）
                objectPoints.push_back(Point3f(-halfArmorWidth, armorHalfHeight, 0));  // 左上
                objectPoints.push_back(Point3f(halfArmorWidth, armorHalfHeight, 0));   // 右上
                objectPoints.push_back(Point3f(halfArmorWidth, -armorHalfHeight, 0));  // 右下
                objectPoints.push_back(Point3f(-halfArmorWidth, -armorHalfHeight, 0)); // 左下

                // 执行 PnP 解算
                Mat rvec, tvec;
                bool success = solvePnP(objectPoints, armorCorners, cameraMatrix, distCoeffs, rvec, tvec, false,
                                        SOLVEPNP_ITERATIVE);

                if (success)
                {
                    // 计算距离
                    double distance =
                        sqrt(tvec.at<double>(0) * tvec.at<double>(0) + tvec.at<double>(1) * tvec.at<double>(1) +
                             tvec.at<double>(2) * tvec.at<double>(2));

                    // 在图像上显示距离和位置信息
                    Point2f centerPoint = (bar1.center + bar2.center) * 0.5;

                    string distText = "Dist: " + to_string(int(distance)) + " mm";
                    string posText = "X:" + to_string(int(tvec.at<double>(0))) +
                                     " Y:" + to_string(int(tvec.at<double>(1))) +
                                     " Z:" + to_string(int(tvec.at<double>(2)));

                    putText(result, distText, Point(centerPoint.x - 50, centerPoint.y - 20), FONT_HERSHEY_SIMPLEX, 0.6,
                            Scalar(0, 255, 255), 2);
                    putText(result, posText, Point(centerPoint.x - 50, centerPoint.y + 5), FONT_HERSHEY_SIMPLEX, 0.5,
                            Scalar(0, 255, 255), 1);

                    // 绘制坐标系
                    vector<Point3f> axisPoints;
                    axisPoints.push_back(Point3f(0, 0, 0));
                    axisPoints.push_back(Point3f(50, 0, 0)); // X轴
                    axisPoints.push_back(Point3f(0, 50, 0)); // Y轴
                    axisPoints.push_back(Point3f(0, 0, 50)); // Z轴

                    vector<Point2f> projectedAxis;
                    projectPoints(axisPoints, rvec, tvec, cameraMatrix, distCoeffs, projectedAxis);

                    // 绘制坐标轴
                    line(result, projectedAxis[0], projectedAxis[1], Scalar(0, 0, 255), 2); // X轴-红色
                    line(result, projectedAxis[0], projectedAxis[2], Scalar(0, 255, 0), 2); // Y轴-绿色
                    line(result, projectedAxis[0], projectedAxis[3], Scalar(255, 0, 0), 2); // Z轴-蓝色

                    cout << "Distance: " << distance << " mm, Position: (" << tvec.at<double>(0) << ", "
                         << tvec.at<double>(1) << ", " << tvec.at<double>(2) << ")" << endl;
                }

                // ============ 透视变换到正面视图 ============
                Mat warpedArmor = warpArmorToFrontView(frame, leftBar, rightBar);

                // 显示变换后的装甲板正面视图并尝试进行分类
                if (!warpedArmor.empty())
                {
                    Mat displayArmor = warpedArmor.clone();
                    auto matcher = armor::getGlobalArmorMatcher();
                    if (matcher && matcher->isReady())
                    {
                        auto matchResult = matcher->match(warpedArmor);
                        if (matchResult.success)
                        {
                            std::string matchText = std::to_string(matchResult.classId) + " (" +
                                                    cv::format("%.2f", matchResult.confidence) + ")";
                            putText(displayArmor, matchResult.label, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7,
                                    Scalar(0, 255, 0), 2);

                            Point2f textAnchor = (bar1.center + bar2.center) * 0.5f;
                            putText(result, matchResult.label, Point(textAnchor.x - 60, textAnchor.y - 40),
                                    FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 165, 255), 2);
                        }
                        else
                        {
                            static int errorThrottle = 0;
                            if (++errorThrottle % 120 == 0)
                            {
                                std::cerr << "ArmorMatcher 推理失败: " << matchResult.error << std::endl;
                            }
                        }
                    }
                    imshow("Armor Front View", displayArmor);
                }

                // 绘制连接线显示配对关系
                line(result, bar1.center, bar2.center, Scalar(0, 255, 255), 1);
            }
        }
    }
}

// 把示例程序的 main 包裹起来，只有当定义了 PROCESS_MAIN 时才会编译为独立程序
#ifdef PROCESS_MAIN
int main(int argc, char **argv) // 或者char* argv[]
{
    // 打开视频文件
    VideoCapture cap("../../resources/blue.mp4");

    if (!cap.isOpened())
    {
        cerr << "无法打开视频文件，请检查路径！" << endl;
        return -1;
    }

    cout << "视频已打开，开始处理..." << endl;

    // 设置缩放比例（0.5表示缩小到原来的50%）
    double scale = 0.7;

    Mat frame, processedFrame, scaledFrame, scaledProcessed, scaledBinary;

    while (true)
    {
        // 读取一帧
        cap >> frame;

        // 如果读取失败（视频结束），退出循环
        if (frame.empty())
        {
            cout << "视频播放完毕！" << endl;
            break;
        }

        // 缩放原始帧
        resize(frame, scaledFrame, Size(), scale, scale, INTER_LINEAR);

        // 处理缩放后的帧，获得二值图和处理后图
        processFrame(scaledFrame, scaledBinary, scaledProcessed);

        // 将二值图转换为BGR以便与彩色图像并排显示
        Mat binaryBGR;
        cvtColor(scaledBinary, binaryBGR, COLOR_GRAY2BGR);

        // 创建并排显示的图像：binary | processed
        Mat combined;
        hconcat(binaryBGR, scaledProcessed, combined);

        // 显示并排的结果
        imshow("Binary | Detected (Press +/- to adjust scale, ESC/q to quit)", combined);

        // 按键控制
        int key = waitKey(30);
        if (key == 27 || key == 'q')
        { // ESC或q键退出
            break;
        }
        else if (key == ' ')
        { // 空格键暂停
            waitKey(0);
        }
        else if (key == '+' || key == '=')
        { // +键放大
            scale = min(scale + 0.1, 2.0);
            cout << "缩放比例: " << scale * 100 << "%" << endl;
        }
        else if (key == '-' || key == '_')
        { // -键缩小
            scale = max(scale - 0.1, 0.2);
            cout << "缩放比例: " << scale * 100 << "%" << endl;
        }
    }

    cap.release();
    destroyAllWindows();

    return 0;
}
#endif
