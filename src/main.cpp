#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem> // C++17 文件系统库
#include <regex>      // 用于正则表达式
#include <map>        // 用于快速查找 Mod 类型
#include <algorithm>  // 用于 std::transform
#include <ctime>      // 用于获取当前时间作为日志时间戳
#include <iomanip>    // 用于 std::put_time
#include <cstdlib>    // 用于 system("pause")
#include "include/nlohmann/json.hpp"

// 针对 Windows 平台的乱码问题, 引入 Windows.h
#ifdef _WIN32
#include <windows.h>
#endif

// 在文件顶部添加这个包含
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// 跨平台的按任意键函数 (最终版)
void pressAnyKeyToExit() {
#ifdef _WIN32
    // 在 Windows 上, system("pause") 是最简单且最可靠的方式
    // 它会自己显示 "Press any key to continue . . ."
    system("pause");
#else
    // Linux/Unix 系统下的实现
    std::cout << "按任意键退出..." << std::endl;

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // 在读取之前清空输入缓冲区, 防止残留的换行符影响
    tcflush(STDIN_FILENO, TCIFLUSH);

    getchar(); // 等待按键

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // 恢复终端设置
#endif
}


// 全局日志文件流
std::ofstream logFile;
// 日志文件名, 现在只是文件名, 完整路径在运行时确定
const std::string LOG_FILENAME_BASE = "mod_classifier.log";

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
        logFile.flush(); // 立即刷新缓冲区, 确保信息写入文件
    }
}

// --- 1. Mod 数据结构定义 ---
enum class ModType {
    ClientOnly,                 // 仅客户端
    ServerOnly,                 // 仅服务端
    ClientRequiredServerOptional, // 客户端必装, 服务端可选
    ClientOptionalServerRequired, // 客户端可选, 服务端必装
    ClientAndServerRequired,    // 客户端和服务端都必装
    ClientOptionalServerOptional,   // 客户端可选, 服务端可选
    Unknown                     // 未知类型 (需在JSON中指定)
};

struct ModInfo {
    std::string name; // Mod 文件名 (这里指干净的名称, 用于匹配 JSON)
    ModType type;     // Mod 类型

    // 辅助函数, 将字符串转换为 ModType 枚举
    static ModType stringToModType(const std::string& typeStr) {
        if (typeStr == "client_only") return ModType::ClientOnly;
        if (typeStr == "server_only") return ModType::ServerOnly;
        if (typeStr == "client_required_server_optional") return ModType::ClientRequiredServerOptional;
        if (typeStr == "client_optional_server_required") return ModType::ClientOptionalServerRequired;
        if (typeStr == "client_and_server_required") return ModType::ClientAndServerRequired;
        if (typeStr == "client_optional_server_optional") return ModType::ClientOptionalServerOptional;
        if (typeStr == "unknown") return ModType::Unknown; // 显式支持 unknown 类型
        return ModType::Unknown; // 默认回退, 但主要依赖JSON的正确性
    }

    // 辅助函数, 将 ModType 枚举转换为对应的目录名
    static std::string modTypeToDirectory(ModType type) {
        switch (type) {
            case ModType::ClientOnly: return "ClientOnly";
            case ModType::ServerOnly: return "ServerOnly";
            case ModType::ClientRequiredServerOptional: return "ClientRequiredServerOptional";
            case ModType::ClientOptionalServerRequired: return "ClientOptionalServerRequired";
            case ModType::ClientAndServerRequired: return "ClientAndServerRequired";
            case ModType::ClientOptionalServerOptional: return "ClientOptionalServerOptional";
            case ModType::Unknown: return "Unknown"; // 处理在JSON中指定的Unknown类型
            default: return "Unknown";
        }
    }
};

// --- 辅助函数：从 Mod 文件名中提取干净的名称 ---
// 排除版本号和方括号内的中文译名
std::string getCleanModName(const std::string& fullFileName) {
    // 找到最后一个点, 分离文件名和扩展名 (例如 ".jar")
    size_t lastDotPos = fullFileName.rfind('.');
    std::string nameWithoutExt;
    std::string extension;

    if (lastDotPos != std::string::npos) {
        nameWithoutExt = fullFileName.substr(0, lastDotPos);
        extension = fullFileName.substr(lastDotPos); // 包含点, 例如 ".jar"
    } else {
        nameWithoutExt = fullFileName;
        extension = "";
    }

    // 1. 移除方括号内的内容
    std::regex bracket_regex("\\[[^\\]]*\\]");
    nameWithoutExt = std::regex_replace(nameWithoutExt, bracket_regex, "");

    // 2a. 移除特定的非标准分隔符, 如 '·'
    // The middle dot character · is encoded as C2 B7 in UTF-8
    nameWithoutExt = std::regex_replace(nameWithoutExt, std::regex("\xC2\xB7"), "");

    // 2b. 处理混合语言前缀
    size_t last_non_ascii_pos = std::string::npos;
    for (int i = nameWithoutExt.length() - 1; i >= 0; --i) {
        if (static_cast<unsigned char>(nameWithoutExt[i]) > 127) {
            last_non_ascii_pos = i;
            break;
        }
    }

    if (last_non_ascii_pos != std::string::npos && last_non_ascii_pos + 1 < nameWithoutExt.length()) {
        std::string suffix_part = nameWithoutExt.substr(last_non_ascii_pos + 1);
        auto it = std::find_if(suffix_part.begin(), suffix_part.end(), [](char c){
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        });

        if (it != suffix_part.end()) {
            nameWithoutExt = suffix_part;
        }
    }

    // 3. 移除文件名开头的 Minecraft 版本号
    std::regex mc_version_prefix_regex("^[0-9]+\\.[0-9]+(?:\\.[0-9]+)*[-_]", std::regex_constants::icase);
    nameWithoutExt = std::regex_replace(nameWithoutExt, mc_version_prefix_regex, "");

    // 4. 移除 "for [加载器名称]" 模式
    std::regex for_loader_regex("\\s+for\\s+[a-zA-Z]+", std::regex_constants::icase);
    nameWithoutExt = std::regex_replace(nameWithoutExt, for_loader_regex, "");

    // 5. 在加载器和数字之间插入空格, 以规范 "forge1.20.1" 这样的名称
    std::regex loader_digit_regex(
            "(forge|fabric|quilt|neoforge|rift|liteloader|nilloader)"
            "([0-9])",
            std::regex_constants::icase
    );
    nameWithoutExt = std::regex_replace(nameWithoutExt, loader_digit_regex, "$1 $2");

    // 6. 迭代移除文件名末尾的版本号、加载器等后缀
    std::regex suffix_regex(
            "[-_+\\s.]"
            "(?:"
            "v?[0-9]+(?:[\\._\\-][0-9a-zA-Z_+-]+)*"
            "|mc[0-9]+(?:\\.[0-9]+)*"
            "|forge|fabric|quilt|neoforge|rift|liteloader|nilloader"
            "|snapshot|pre|rc|beta|alpha"
            "|universal|all"
            ")"
            "\\s*$", std::regex_constants::icase
    );

    std::string tempName = nameWithoutExt;
    std::string prevName;
    do {
        prevName = tempName;
        tempName = std::regex_replace(tempName, suffix_regex, "");
    } while (tempName != prevName);
    nameWithoutExt = tempName;

    // 7. 移除多余的空格, 并修剪首尾空格和分隔符
    nameWithoutExt = std::regex_replace(nameWithoutExt, std::regex(" +"), " ");
    size_t first = nameWithoutExt.find_first_not_of(" -_");
    if (std::string::npos == first) {
        nameWithoutExt = "";
    } else {
        size_t last = nameWithoutExt.find_last_not_of(" -_");
        nameWithoutExt = nameWithoutExt.substr(first, (last - first + 1));
    }

    // 8. 将清理后的名称转换为小写
    std::transform(nameWithoutExt.begin(), nameWithoutExt.end(), nameWithoutExt.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    return nameWithoutExt + extension;
}

// --- 2. JSON 读写 ---
std::vector<ModInfo> readModDataFromJson(const std::string& filePath) {
    std::vector<ModInfo> mods;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logMessage("无法打开 JSON 文件: " + filePath, true);
        return mods;
    }

    try {
        json data = json::parse(file);
        if (!data.is_array()) {
            logMessage("JSON 文件内容不是一个有效的数组。", true);
            file.close();
            return mods;
        }
        for (const auto& item : data) {
            if (item.is_object() && item.count("name") && item.count("type")) {
                ModInfo mod;
                mod.name = item.at("name").get<std::string>();
                std::transform(mod.name.begin(), mod.name.end(), mod.name.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                mod.type = ModInfo::stringToModType(item.at("type").get<std::string>());
                mods.push_back(mod);
            } else {
                logMessage("JSON 文件中存在无效的 Mod 条目, 已跳过。", true);
            }
        }
    } catch (const json::exception& e) {
        logMessage("解析 JSON 文件失败: " + std::string(e.what()), true);
    }
    file.close();
    return mods;
}

// --- 3. Mod 分类逻辑 & 4. 文件操作 ---
void classifyMods(const std::vector<ModInfo>& mods, const std::string& inputDir, const std::string& outputDir) {
    // 确保输出目录和所有可能的子目录都存在
    fs::create_directories(outputDir);
    fs::create_directories(fs::path(outputDir) / "ClientOnly");
    fs::create_directories(fs::path(outputDir) / "ServerOnly");
    fs::create_directories(fs::path(outputDir) / "ClientRequiredServerOptional");
    fs::create_directories(fs::path(outputDir) / "ClientOptionalServerRequired");
    fs::create_directories(fs::path(outputDir) / "ClientAndServerRequired");
    fs::create_directories(fs::path(outputDir) / "ClientOptionalServerOptional");
    fs::create_directories(fs::path(outputDir) / "Unknown"); // 为在JSON中指定的Unknown类型创建目录

    // 创建一个映射, 用于快速查找 Mod 类型
    std::map<std::string, ModType> modTypeMap;
    for (const auto& mod : mods) {
        modTypeMap[mod.name] = mod.type;
    }

    // 遍历 Input 目录中的所有文件
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.is_regular_file()) {
            std::string fullFileName = entry.path().filename().string();
            std::string cleanFileName = getCleanModName(fullFileName);

            // 在映射中查找干净的 Mod 名称
            auto it = modTypeMap.find(cleanFileName);
            if (it != modTypeMap.end()) {
                // 找到了匹配项, 进行分类
                ModType type = it->second;
                std::string targetSubDir = ModInfo::modTypeToDirectory(type);
                fs::path destinationPath = fs::path(outputDir) / targetSubDir / fullFileName;

                // 检查目标文件是否已存在
                if (fs::exists(destinationPath) && fs::is_regular_file(destinationPath)) {
                    logMessage("已跳过 Mod: " + fullFileName + ", 因为它已存在于目标目录: " + targetSubDir);
                    continue;
                }

                try {
                    fs::copy(entry.path(), destinationPath, fs::copy_options::overwrite_existing);
                    logMessage("已分类 Mod: " + fullFileName + " 到 " + targetSubDir);
                } catch (const fs::filesystem_error& e) {
                    logMessage("无法分类 Mod " + fullFileName + ": " + e.what(), true);
                }
            } else {
                // 未找到匹配项, 记录错误, 不移动文件
                logMessage("未在 mods_data.json 中找到 Mod 的分类信息: " + fullFileName + " (干净名称: " + cleanFileName + ")", true);
            }
        }
    }
}


int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    fs::path executablePath;
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    executablePath = fs::path(buffer);
#else
    if (argc > 0) {
        executablePath = fs::path(argv[0]);
    }
#endif

    fs::path logFilePath;
    if (executablePath.has_parent_path()) {
        logFilePath = executablePath.parent_path() / LOG_FILENAME_BASE;
    } else {
        logFilePath = LOG_FILENAME_BASE;
    }

    logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    if (!logFile.is_open()) {
        std::cerr << "错误: 无法打开日志文件: " << logFilePath << std::endl;
    }

    logMessage("程序启动。");

    std::string inputDirectory = "Input";
    std::string outputDirectory = "Output";
    std::string jsonDataFile = "mods_data.json";

    if (!fs::exists(inputDirectory)) {
        logMessage("检测到 'Input' 文件夹不存在, 正在创建...", false);
        if (!fs::create_directories(inputDirectory)) {
            logMessage("无法创建 'Input' 文件夹。", true);
            logFile.close();
            pressAnyKeyToExit();
            return 1;
        }
    } else if (!fs::is_directory(inputDirectory)) {
        logMessage("'Input' 路径存在但不是一个目录。", true);
        logFile.close();
        pressAnyKeyToExit();
        return 1;
    }

    if (!fs::exists(jsonDataFile)) {
        logMessage("检测到 'mods_data.json' 文件不存在, 正在创建...", false);
        std::ofstream outFile(jsonDataFile);
        if (outFile.is_open()) {
            outFile << "[]";
            outFile.close();
            logMessage("'mods_data.json' 已成功创建和初始化。", false);
        } else {
            logMessage("无法创建或写入 'mods_data.json' 文件。", true);
            logFile.close();
            pressAnyKeyToExit();
            return 1;
        }
    } else if (!fs::is_regular_file(jsonDataFile)) {
        logMessage("'mods_data.json' 路径存在但不是一个文件。", true);
        logFile.close();
        pressAnyKeyToExit();
        return 1;
    }

    logMessage("正在读取 Mod 数据...");
    std::vector<ModInfo> mods = readModDataFromJson(jsonDataFile);

    if (mods.empty()) {
        logMessage("没有从 JSON 文件中读取到 Mod 数据, 文件可能为空或有误。", false);
    }

    logMessage("开始分类 Mod...");
    classifyMods(mods, inputDirectory, outputDirectory);

    logMessage("Mod 分类完成！", false);

    pressAnyKeyToExit();

    logFile.close();
    return 0;
}