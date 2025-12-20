#include "image_utils.h"
#include <vector>
#include <iostream>
#include <algorithm> // for std::reverse (optional) or manual loop

// 这是一个单头文件库，必须在一个 CPP 文件中定义 IMPLEMENTATION 宏
#include <stb_image_write.h>
#include <glad/gl.h> // 需要包含 OpenGL 头文件以使用 glReadPixels

void ImageUtils::saveScreenshot(const std::string& filename, int width, int height)
{
    if (width <= 0 || height <= 0) return;

    // 1. 分配内存 (RGBA)
    std::vector<unsigned char> pixels(width * height * 4);

    // 2. 读取像素
    // 确保字节对齐
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // 3. 上下翻转 (OpenGL 左下角 vs 图片 左上角)
    // 这一步是必须的，否则保存出来的图片是倒着的
    int rowSize = width * 4;
    std::vector<unsigned char> flippedPixels(width * height * 4);

    for (int y = 0; y < height; ++y)
    {
        unsigned char* srcRow = pixels.data() + y * rowSize;
        unsigned char* dstRow = flippedPixels.data() + (height - 1 - y) * rowSize;
        memcpy(dstRow, srcRow, rowSize);
    }

    // 4. 判断格式并保存
    bool success = false;
    std::string ext = filename.substr(filename.find_last_of(".") + 1);
    
    if (ext == "png") {
        success = stbi_write_png(filename.c_str(), width, height, 4, flippedPixels.data(), rowSize);
    } else if (ext == "jpg" || ext == "jpeg") {
        success = stbi_write_jpg(filename.c_str(), width, height, 4, flippedPixels.data(), 90); // 90 quality
    } else {
        // 默认存 PNG
        success = stbi_write_png((filename + ".png").c_str(), width, height, 4, flippedPixels.data(), rowSize);
    }

    if (success) {
        std::cout << "[ImageUtils] Screenshot saved to " << filename << std::endl;
    } else {
        std::cerr << "[ImageUtils] Failed to save screenshot: " << filename << std::endl;
    }
}