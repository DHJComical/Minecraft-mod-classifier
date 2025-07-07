# Minecraft-mod-classifier

## 项目简介
- 这个项目是一个用 C++ 编写的命令行工具，旨在帮助 Minecraft 玩家或服务器管理员自动分类他们的 Mod 文件。它根据 Mod 的运行端属性（例如仅客户端、仅服务端、客户端和服务器都需要等）将 Mod 分类到不同的输出文件夹中，从而简化 Mod 管理过程。为图方便作者使用Gemini Pro和Claude4进行编程后自己修补，因此有大量中文注解

## 主要功能
- 灵活的 Mod 分类: 支持将 Mod 分为以下五种主要类型：
    - 仅客户端 (ClientOnly)
    - 仅服务端 (ServerOnly)
    - 客户端必装，服务端可选 (ClientRequiredServerOptional)
    - 客户端可选，服务端必装 (ClientOptionalServerRequired)
    - 客户端和服务端都必装 (ClientAndServerRequired)
    - 客户端和服务端都可选 (ClientOptionalServerOptional)
- 智能文件名清理: 程序能够自动处理 Mod 文件名中常见的干扰信息，如版本号（例如 1.16.5-1.0.0）、Minecraft 版本（例如 mc1.12）以及方括号内的中文译名（例如 [我的模组]），确保能与 mods_data.json 中定义的“干净”Mod 名称进行准确匹配。
- 日志系统: 所有重要的程序运行信息、分类结果、警告和错误都会被记录到 mod_classifier.log 文件中，方便用户查看和调试。

## 如何使用
- 在[Release](https://github.com/DHJComical/Minecraft-mod-classifier/releases)里下载最新发行版的Minecraft-mod-classifier.exe和mods_data.json(你也可以在项目文件里获得最新的mods_data.json)
- 将它们放入一个文件夹，运行Minecraft-mod-classifier.exe此时会创建Input和Output文件夹
- 将所有Mod的jar文件放到Input文件夹里，再次运行Minecraft-mod-classifier.exe
- 从Output里取出分类好的文件

## 贡献
- 这个项目和万用汉化包一样，是一个要靠社区的项目，欢迎任何人提交mods_data.json以更新分类资料

## 编译
- 需要安装CMake及任意C++编译器
- 导入CLion等运行编译

## 第三方库
- [nlohmann/json](https://github.com/nlohmann/json)
