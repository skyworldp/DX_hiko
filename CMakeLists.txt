cmake_minimum_required(VERSION 3.10)
project(hiko)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
# 海康威视 MVS SDK 路径配置
# 请根据实际安装路径修改
set(MVS_SDK_PATH "/opt/MVS")
# 包含头文件目录
include_directories(
    ${MVS_SDK_PATH}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接库目录
link_directories(
    ${MVS_SDK_PATH}/lib/64
)

# 查找 OpenCV (用于图像显示)

# 优先查找指定版本（REQUIRED 会在找不到时终止配置）
find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    message(STATUS "Found OpenCV ${OpenCV_VERSION} at ${OpenCV_DIR}")
    include_directories(${OpenCV_INCLUDE_DIRS})
    add_definitions(-DUSE_OPENCV)
    add_library(armor_matcher STATIC ArmorMatcher.cpp)
    target_include_directories(armor_matcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # 保持兼容性：OpenCV 仍会提供 ${OpenCV_LIBS}，也可使用 OpenCV:: components
    target_link_libraries(armor_matcher PUBLIC ${OpenCV_LIBS})
endif()

# 添加主程序
add_executable(${PROJECT_NAME}
    main.cpp
    HikCamera.cpp
    process.cpp
)

# 链接海康威视 MVS SDK 库
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        MvCameraControl
        ${OpenCV_LIBS}
)

if(OpenCV_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE armor_matcher)
endif()



