//
// Created by Comical on 2025/7/6.
//
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem> // C++17 文件系统库
#include <regex>      // 用于正则表达式
#include <map>        // 用于快速查找 Mod 类型
#include "include/nlohmann/json.hpp" // 引入 nlohmann/json 头文件

namespace fs = std::filesystem;
using json = nlohmann::json;

// 全局日志文件流
std::ofstream logFile;
const std::string LOG_FILENAME = "mod_classifier.log"; // 日志文件名

// --- 辅助函数：输出日志信息到控制台和文件 ---
void logMessage(const std::string& message, bool isError = false) {
    // 获取当前时间作为时间戳
    std::time_t now = std::time(nullptr);
    std::tm* ltm = std::localtime(&now);

    // 格式化时间戳
    std::stringstream ss;
    ss << std::put_time(ltm, "[%Y-%m-%d %H:%M:%S]");

    // 输出到控制台
    if (isError) {
        std::cerr << ss.str() << " 错误: " << message << std::endl;
    } else {
        std::cout << ss.str() << " 信息: " << message << std::endl;
    }

    // 输出到日志文件
    if (logFile.is_open()) {
        logFile << ss.str() << " " << (isError ? "错误" : "信息") << ": " << message << std::endl;
    }
}
// --- 1. Mod 数据结构定义 ---
enum class ModType {
    ClientOnly,                 // 仅客户端
    ServerOnly,                 // 仅服务端
    ClientRequiredServerOptional, // 客户端必装，服务端可选
    ClientOptionalServerRequired, // 客户端可选，服务端必装
    ClientAndServerRequired,    // 客户端和服务端都必装
    Unknown                     // 未知类型
};

struct ModInfo {
    std::string name; // Mod 文件名 (这里指干净的名称，用于匹配 JSON)
    ModType type;     // Mod 类型

    // 辅助函数，将字符串转换为 ModType 枚举
    static ModType stringToModType(const std::string& typeStr) {
        if (typeStr == "client_only") return ModType::ClientOnly;
        if (typeStr == "server_only") return ModType::ServerOnly;
        if (typeStr == "client_required_server_optional") return ModType::ClientRequiredServerOptional;
        if (typeStr == "client_optional_server_required") return ModType::ClientOptionalServerRequired;
        if (typeStr == "client_and_server_required") return ModType::ClientAndServerRequired;
        return ModType::Unknown;
    }

    // 辅助函数，将 ModType 枚举转换为对应的目录名
    static std::string modTypeToDirectory(ModType type) {
        switch (type) {
            case ModType::ClientOnly: return "ClientOnly";
            case ModType::ServerOnly: return "ServerOnly";
            case ModType::ClientRequiredServerOptional: return "ClientRequiredServerOptional";
            case ModType::ClientOptionalServerRequired: return "ClientOptionalServerRequired";
            case ModType::ClientAndServerRequired: return "ClientAndServerRequired";
            default: return "Unknown"; // 理论上不应该发生，除非有未处理的类型
        }
    }
};

// --- 辅助函数：从 Mod 文件名中提取干净的名称 ---
// 排除版本号和方括号内的中文译名
std::string getCleanModName(const std::string& fullFileName) {
    // 找到最后一个点，分离文件名和扩展名 (例如 ".jar")
    size_t lastDotPos = fullFileName.rfind('.');
    std::string nameWithoutExt;
    std::string extension;

    if (lastDotPos != std::string::npos) {
        nameWithoutExt = fullFileName.substr(0, lastDotPos);
        extension = fullFileName.substr(lastDotPos); // 包含点，例如 ".jar"
    } else {
        nameWithoutExt = fullFileName;
        extension = "";
    }

    // 1. 移除方括号内的内容 (例如 "[中文译名]", "-[中文译名]")，无论其在字符串的哪个位置
    // 正则表达式: `\\[[^\\]]*\\]` 匹配 `[` 开头，`]` 结尾，中间是非 `]` 的任意字符
    std::regex bracket_regex("\\[[^\\]]*\\]");
    nameWithoutExt = std::regex_replace(nameWithoutExt, bracket_regex, "");

    // 2. 移除常见的版本号、Minecraft版本和加载器后缀
    // 这个正则表达式更通用，旨在移除文件名中常见的版本信息、Minecraft版本（如mc1.12）和加载器标识符
    // 匹配模式包括:
    //   - `[-_]v?[0-9]+(?:\.[0-9]+)*(?:[a-zA-Z0-9.\-_]*)?` (例如: -1.0.0, -v1.0, _1.16.5-forge)
    //   - `[-_]mc[0-9]+(?:\.[0-9]+)*` (例如: -mc1.12, _mc1.16.5)
    //   - `[-_](?:forge|fabric|quilt|neoforge|rift|liteloader|snapshot|pre|rc|beta|alpha)` (例如: -forge, -fabric)
    //   - 组合模式，例如加载器后可选的版本号
    // 注意: 使用 `|` 连接多个模式，并使用 `(?:...)` 创建非捕获组
    std::regex version_and_loader_regex(
            "[-_](?:v)?[0-9]+(?:\\.[0-9]+)*(?:[a-zA-Z0-9.\\-_]*)?" // 常见版本号，如 -1.0.0, -v1.0, _1.16.5-forge
            "|[-_]mc[0-9]+(?:\\.[0-9]+)*" // Minecraft 版本号，如 -mc1.12, _mc1.16.5
            "|[-_](?:forge|fabric|quilt|neoforge|rift|liteloader|snapshot|pre|rc|beta|alpha)" // 加载器或发布阶段标识符
            "(?:[-_](?:v)?[0-9]+(?:\\.[0-9]+)*(?:[a-zA-Z0-9.\\-_]*)?)?" // 加载器后可选的版本号
            , std::regex_constants::icase // 忽略大小写匹配
    );
    nameWithoutExt = std::regex_replace(nameWithoutExt, version_and_loader_regex, "");


    // 3. 最终检查：如果清理后的名称以连字符或下划线结尾，则移除它
    while (!nameWithoutExt.empty() && (nameWithoutExt.back() == '-' || nameWithoutExt.back() == '_')) {
        nameWithoutExt.pop_back();
    }

    // 4. 移除多余的空格，并修剪首尾空格
    // 移除连续的空格
    nameWithoutExt = std::regex_replace(nameWithoutExt, std::regex(" +"), " ");
    // 移除首尾空格
    size_t first = nameWithoutExt.find_first_not_of(' ');
    if (std::string::npos == first) {
        nameWithoutExt = "";
    } else {
        size_t last = nameWithoutExt.find_last_not_of(' ');
        nameWithoutExt = nameWithoutExt.substr(first, (last - first + 1));
    }

    // 5. 将清理后的名称转换为小写，以便与 JSON 中的名称进行大小写不敏感的匹配
    std::transform(nameWithoutExt.begin(), nameWithoutExt.end(), nameWithoutExt.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    return nameWithoutExt + extension; // 重新添加扩展名
}

// --- 2. JSON 读写 ---
std::vector<ModInfo> readModDataFromJson(const std::string& filePath) {
    std::vector<ModInfo> mods;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "错误: 无法打开 JSON 文件: " << filePath << std::endl;
        return mods;
    }

    try {
        json data = json::parse(file);
        // 确保解析的是一个 JSON 数组
        if (!data.is_array()) {
            std::cerr << "错误: JSON 文件内容不是一个有效的数组。请确保 'mods_data.json' 的根元素是一个 JSON 数组。" << std::endl;
            file.close();
            return mods;
        }
        for (const auto& item : data) {
            // 检查每个元素是否是对象且包含必要的字段
            if (item.is_object() && item.count("name") && item.count("type")) {
                ModInfo mod;
                mod.name = item.at("name").get<std::string>();
                mod.type = ModInfo::stringToModType(item.at("type").get<std::string>());
                mods.push_back(mod);
            } else {
                std::cerr << "警告: JSON 文件中存在无效的 Mod 条目，已跳过。请确保每个 Mod 条目都是一个包含 'name' 和 'type' 字段的对象。" << std::endl;
            }
        }
    } catch (const json::exception& e) {
        std::cerr << "错误: 解析 JSON 文件失败: " << e.what() << std::endl;
    }
    file.close();
    return mods;
}

// --- 3. Mod 分类逻辑 & 4. 文件操作 ---
void classifyMods(const std::vector<ModInfo>& mods, const std::string& inputDir, const std::string& outputDir) {
    // 确保输出目录存在，并创建子目录
    fs::create_directories(outputDir);
    fs::create_directories(fs::path(outputDir) / "ClientOnly");
    fs::create_directories(fs::path(outputDir) / "ServerOnly");
    fs::create_directories(fs::path(outputDir) / "ClientRequiredServerOptional");
    fs::create_directories(fs::path(outputDir) / "ClientOptionalServerRequired");
    fs::create_directories(fs::path(outputDir) / "ClientAndServerRequired");

    // 创建一个映射，用于通过干净的 Mod 名称快速查找 Mod 类型
    // 这里的 mod.name 应该是 mods_data.json 中定义的干净名称
    std::map<std::string, ModType> modTypeMap;
    for (const auto& mod : mods) {
        modTypeMap[mod.name] = mod.type;
    }

    // 遍历 Input 目录中的所有文件
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.is_regular_file()) { // 确保是文件而不是目录
            std::string fullFileName = entry.path().filename().string(); // 获取完整文件名 (例如 "modname-1.16.5-[译名].jar")
            std::string cleanFileName = getCleanModName(fullFileName); // 获取干净的 Mod 名称 (例如 "modname.jar")

            // 在映射中查找干净的 Mod 名称
            auto it = modTypeMap.find(cleanFileName);
            if (it != modTypeMap.end()) {
                // 找到匹配项
                ModType type = it->second;
                std::string targetSubDir = ModInfo::modTypeToDirectory(type);
                fs::path destinationPath = fs::path(outputDir) / targetSubDir / fullFileName; // 目标路径使用原始文件名

                try {
                    // 复制文件，如果目标已存在则覆盖
                    fs::copy(entry.path(), destinationPath, fs::copy_options::overwrite_existing);
                    // 如果你想移动而不是复制，可以使用 fs::rename(entry.path(), destinationPath);
                    logMessage("已分类 Mod: " + fullFileName + " (干净名称: " + cleanFileName + ") 到 " + targetSubDir); // 使用日志系统
                } catch (const fs::filesystem_error& e) {
                    logMessage("无法分类 Mod " + fullFileName + ": " + e.what(), true); // 使用日志系统
                }
            } else {
                logMessage("未在 mods_data.json 中找到 Mod 的分类信息: " + fullFileName + " (干净名称: " + cleanFileName + ")", true); // 使用日志系统
            }
        }
    }
}

int main() {
    system("chcp 65001");

    // 打开日志文件，以追加模式写入
    logFile.open(LOG_FILENAME, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "错误: 无法打开日志文件: " << LOG_FILENAME << std::endl;
        // 即使无法打开日志文件，程序也尝试继续运行，但只输出到控制台
    }

    logMessage("程序启动。"); // 记录程序启动

    std::string inputDirectory = "Input";
    std::string outputDirectory = "Output";
    std::string jsonDataFile = "mods_data.json";

    // 检查 Input 目录是否存在，如果不存在则创建
    if (!fs::exists(inputDirectory)) {
        logMessage("检测到 'Input' 文件夹不存在，正在创建...");
        if (!fs::create_directories(inputDirectory)) {
            logMessage("无法创建 'Input' 文件夹。请检查权限或路径。", true);
            logFile.close(); // 关闭日志文件
            return 1;
        }
    } else if (!fs::is_directory(inputDirectory)) {
        logMessage("'Input' 路径存在但不是一个目录。请检查。", true);
        logFile.close(); // 关闭日志文件
        return 1;
    }

    // 检查 mods_data.json 是否存在，如果不存在则创建并写入空数组
    if (!fs::exists(jsonDataFile)) {
        logMessage("检测到 'mods_data.json' 文件不存在，正在创建并初始化为空数组...", false);
        std::ofstream outFile(jsonDataFile);
        if (outFile.is_open()) {
            outFile << "[]"; // 写入一个空的 JSON 数组
            outFile.close();
            logMessage("'mods_data.json' 已成功创建和初始化。", false);
        } else {
            logMessage("无法创建或写入 'mods_data.json' 文件。请检查权限或路径。", true);
            logFile.close(); // 关闭日志文件
            return 1;
        }
    } else if (!fs::is_regular_file(jsonDataFile)) {
        logMessage("'mods_data.json' 路径存在但不是一个文件。请检查。", true);
        logFile.close(); // 关闭日志文件
        return 1;
    }

    logMessage("正在读取 Mod 数据...");
    std::vector<ModInfo> mods = readModDataFromJson(jsonDataFile);

    if (mods.empty()) {
        logMessage("没有从 JSON 文件中读取到 Mod 数据，或者文件为空/有误。请确保 'mods_data.json' 包含有效的 Mod 信息。");
    }

    logMessage("开始分类 Mod...");
    classifyMods(mods, inputDirectory, outputDirectory);

    logMessage("Mod 分类完成！"); // 记录程序完成

    // 添加这一行，等待用户按任意键才关闭窗口
    std::cout << "按任意键退出..." << std::endl; // Prompt user to press a key
    std::cin.ignore(); // 忽略缓冲区中可能存在的任何字符（例如之前的换行符）
    std::cin.get();    // 等待用户输入一个字符 (即按任意键)

    logFile.close(); // 程序结束前关闭日志文件
    return 0;
}