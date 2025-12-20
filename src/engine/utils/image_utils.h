#pragma once

#include <string>

class ImageUtils
{
public:
    // 截取当前 OpenGL 绑定的 Framebuffer 到文件
    // width, height: 图像大小
    // filename: 保存路径 (支持 .png, .jpg)
    static void saveScreenshot(const std::string& filename, int width, int height);
};