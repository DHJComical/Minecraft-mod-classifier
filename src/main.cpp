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
#include "include/nlohmann/json.hpp"

// 针对 Windows 平台的乱码问题，引入 Windows.h
#ifdef _WIN32
#include <windows.h>
#endif


// 在文件顶部添加这个包含
#ifdef _WIN32
#include <conio.h>  // 用于 _getch() 函数
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// 跨平台的按任意键函数
void pressAnyKeyToExit() {
#ifdef _WIN32
    std::cout << "按任意键退出..." << std::endl;
    _getch(); // Windows 下直接使用 _getch()，不需要按回车
#else
    // Linux/Unix 系统下的实现
    std::cout << "按任意键退出..." << std::endl;

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    getchar(); // 等待按键

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // 恢复终端设置
#endif
}


// 全局日志文件流
std::ofstream logFile;
// 日志文件名，现在只是文件名，完整路径在运行时确定
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
        logFile.flush(); // 立即刷新缓冲区，确保信息写入文件
    }
}

// --- 1. Mod 数据结构定义 ---
enum class ModType {
    ClientOnly,                 // 仅客户端
    ServerOnly,                 // 仅服务端
    ClientRequiredServerOptional, // 客户端必装，服务端可选
    ClientOptionalServerRequired, // 客户端可选，服务端必装
    ClientAndServerRequired,    // 客户端和服务端都必装
    ClientOptionalServerOptional,   // 客户端可选，服务端可选
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
        if (typeStr == "client_optional_server_optional") return ModType::ClientOptionalServerOptional;
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
            case ModType::ClientOptionalServerOptional: return "ClientOptionalServerOptional";
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

    // 2. 移除文件名开头的 Minecraft 版本号 (如 1.12.2-, 1.16.5- 等)
    // 匹配模式：开头的数字.数字.数字格式，后跟连字符
    std::regex mc_version_prefix_regex("^[0-9]+\\.[0-9]+(?:\\.[0-9]+)*[-_]", std::regex_constants::icase);
    nameWithoutExt = std::regex_replace(nameWithoutExt, mc_version_prefix_regex, "");

    // 3. 移除 "for [加载器名称]" 模式 (例如 "for NilLoader", "for Forge", "for Fabric")
    // 匹配模式：空格或连字符后跟 "for" 再跟空格和加载器名称
    std::regex for_loader_regex("\\s+for\\s+[a-zA-Z]+", std::regex_constants::icase);
    nameWithoutExt = std::regex_replace(nameWithoutExt, for_loader_regex, "");

    // 4. 迭代移除文件名末尾的版本号、Minecraft版本、加载器后缀和通用标签
    // 匹配模式：
    //   - `[-_+\\s.]` 作为分隔符 (允许连字符、下划线、加号、空格、点)
    //   - 后跟：
    //     - `v?[0-9]+(?:[\\._\\-][0-9a-zA-Z_+-]+)*` (标准版本号，如 -1.0.0, +1.20.1, -v1.0.0-beta)
    //     - `mc[0-9]+(?:\\.[0-9]+)*` (Minecraft 版本号，如 -mc1.16.5)
    //     - `forge|fabric|quilt|neoforge|rift|liteloader|nilloader` (精确匹配的加载器名称)
    //     - `snapshot|pre|rc|beta|alpha` (精确匹配的发布阶段)
    //     - `universal|all` (精确匹配的通用标签)
    //   - `$` 确保匹配发生在字符串的末尾
    std::regex suffix_regex(
            "[-_+\\s.]" // Delimiters: hyphen, underscore, plus, space, DOT
            "(?:" // Start non-capturing group for the patterns to remove
            "v?[0-9]+(?:[\\._\\-][0-9a-zA-Z_+-]+)*" // Standard version, e.g., -1.0.0, +1.20.1, -v1.0.0-beta
            "|mc[0-9]+(?:\\.[0-9]+)*" // Minecraft version, e.g., -mc1.16.5
            "|forge|fabric|quilt|neoforge|rift|liteloader|nilloader" // Exact loader names (added nilloader)
            "|snapshot|pre|rc|beta|alpha" // Exact release stages
            "|universal|all" // Exact common tags
            ")" // End non-capturing group for patterns
            "$", std::regex_constants::icase // Anchor to the end, ignore case
    );

    // 循环移除匹配的后缀，直到没有更多匹配
    std::string tempName = nameWithoutExt;
    std::string prevName;
    do {
        prevName = tempName; // 记录当前状态
        tempName = std::regex_replace(tempName, suffix_regex, ""); // 尝试移除一个后缀
    } while (tempName != prevName); // 如果 tempName 改变了，说明有匹配并被移除，继续循环
    nameWithoutExt = tempName;

    // 5. 移除多余的空格，并修剪首尾空格
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

    // 6. 将清理后的名称转换为小写，以便与 JSON 中的名称进行大小写不敏感的匹配
    std::transform(nameWithoutExt.begin(), nameWithoutExt.end(), nameWithoutExt.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    return nameWithoutExt + extension; // 重新添加扩展名
}

// --- 2. JSON 读写 ---
std::vector<ModInfo> readModDataFromJson(const std::string& filePath) {
    std::vector<ModInfo> mods;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logMessage("无法打开 JSON 文件: " + filePath, true); // 使用日志系统
        return mods;
    }

    try {
        json data = json::parse(file);
        // 确保解析的是一个 JSON 数组
        if (!data.is_array()) {
            logMessage("JSON 文件内容不是一个有效的数组。请确保 'mods_data.json' 的根元素是一个 JSON 数组。", true); // 使用日志系统
            file.close();
            return mods;
        }
        for (const auto& item : data) {
            // 检查每个元素是否是对象且包含必要的字段
            if (item.is_object() && item.count("name") && item.count("type")) {
                ModInfo mod;
                mod.name = item.at("name").get<std::string>();
                // 将从 JSON 读取的 Mod 名称也转换为小写，以确保匹配时的一致性
                std::transform(mod.name.begin(), mod.name.end(), mod.name.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                mod.type = ModInfo::stringToModType(item.at("type").get<std::string>());
                mods.push_back(mod);
            } else {
                logMessage("JSON 文件中存在无效的 Mod 条目，已跳过。请确保每个 Mod 条目都是一个包含 'name' 和 'type' 字段的对象。", true); // 使用日志系统
            }
        }
    } catch (const json::exception& e) {
        logMessage("解析 JSON 文件失败: " + std::string(e.what()), true); // 使用日志系统
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
    fs::create_directories(fs::path(outputDir) / "ClientOptionalServerOptional"); // 新增目录创建

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

                // 检查目标文件是否已存在
                if (fs::exists(destinationPath) && fs::is_regular_file(destinationPath)) {
                    logMessage("已跳过 Mod: " + fullFileName + "，因为它已存在于目标目录: " + targetSubDir);
                    continue; // 跳过当前 Mod，不进行复制操作
                }

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

int main(int argc, char* argv[]) { // main函数现在接收命令行参数
    // 针对 Windows 平台设置控制台输出编码为 UTF-8，解决中文乱码问题
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    // 获取当前可执行文件的目录，用于确定日志文件的位置
    fs::path executablePath;
    #ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    executablePath = fs::path(buffer);
    #endif

    fs::path logFilePath = executablePath.parent_path() / LOG_FILENAME_BASE;

    // 打开日志文件，以截断模式写入，这将清除文件内容。
    // 然后，后续的写入操作将从文件开头开始。
    logFile.open(logFilePath, std::ios::out | std::ios::trunc); // 使用 std::ios::trunc 清除文件内容
    if (!logFile.is_open()) {
        std::cerr << "错误: 无法打开日志文件: " << logFilePath << std::endl;
        // 即使无法打开日志文件，程序也尝试继续运行，但只输出到控制台
    }

    logMessage("程序启动。"); // 记录程序启动

    std::string inputDirectory = "Input";
    std::string outputDirectory = "Output";
    std::string jsonDataFile = "mods_data.json";

    // 检查 Input 目录是否存在，如果不存在则创建
    if (!fs::exists(inputDirectory)) {
        logMessage("检测到 'Input' 文件夹不存在，正在创建...", false);
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
    // 注意：这里读取的 mods_data.json 仍然是相对于当前工作目录的
    // 而 CMake 复制的 mods_data.json 是在 build 目录
    // 确保你的 mods_data.json 放在项目根目录，这样程序才能找到它
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
        logMessage("没有从 JSON 文件中读取到 Mod 数据，或者文件为空/有误。请确保 'mods_data.json' 包含有效的 Mod 信息。", false);
    }

    logMessage("开始分类 Mod...");
    classifyMods(mods, inputDirectory, outputDirectory);

    logMessage("Mod 分类完成！", false); // 记录程序完成

    // 使用新的按任意键函数
    pressAnyKeyToExit();

    logFile.close(); // 程序结束前关闭日志文件
    return 0;
}