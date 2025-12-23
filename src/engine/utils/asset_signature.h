#pragma once

#include <filesystem>
#include <string>

// 用于标识资源文件的唯一版本（指纹）
struct AssetSignature
{
    // 使用最后修改时间 + 文件大小作为快速指纹
    // 这比每次计算 MD5 要快得多，且足以覆盖 99.9% 的修改场景
    std::filesystem::file_time_type lastWriteTime;
    uintmax_t fileSize = 0;
    bool isValid = false;

    // 从磁盘文件生成签名
    static AssetSignature generate(const std::string& filePath)
    {
        AssetSignature sig;
        std::error_code ec;
        
        // 1. 获取文件状态
        if (std::filesystem::exists(filePath, ec) && !std::filesystem::is_directory(filePath, ec))
        {
            sig.lastWriteTime = std::filesystem::last_write_time(filePath, ec);
            sig.fileSize = std::filesystem::file_size(filePath, ec);
            sig.isValid = true;
        }
        return sig;
    }

    // 比较两个签名是否一致 (重载 != 运算符)
    bool operator!=(const AssetSignature& other) const
    {
        if (!isValid || !other.isValid) return true; // 任何一个无效都视为不匹配
        return lastWriteTime != other.lastWriteTime || fileSize != other.fileSize;
    }
    
    bool operator==(const AssetSignature& other) const
    {
        return !(*this != other);
    }
};