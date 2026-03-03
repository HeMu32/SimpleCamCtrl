# 开发与 AI 代理指南

这是一个更大项目的子模块, 提供一个简单的相机控制接口, 包括控制相机释放快门, 录制视频, 以及调整相机曝光设置. 

## 系统环境
- **操作系统**: Windows 10/11 (64-bit)
- **编译器**: MinGW-w64, 9.0.0
- **构建工具**: CMake 3.31
- **Shell**: Windows Powershell
    请根据系统环境, 留意使用的Shell命令, API等. 

## 🏗️ 架构与边界
- **C++ 与 CMake**: 本项目基于 C++17 并使用 CMake 管理构建过程。主要的第三方依赖都应该存放在 `3rdparty/` 目录中。项目的源代码应该放在 `src/` 目录中，测试代码放在 `tests/` 目录中，demo代码放在 `demo/` 目录中。

## 💻 核心模式与代码规范

### 代码规范
请遵循 Microsoft / ANSI 样式的代码缩进, 对合适的变量使用匈牙利标记命名. 另外, 为代码添加Doxygen风格的注释, 以便后续生成文档. 

## 参考资料
- **Sony Camera Remote Commands v2.00**: 这是一个官方提供的文档, 能提供私有PTP指令的参考, 包含了相机控制的示例代码和文档. API文档在 `\3rdparty\CamRmCmd_Demos\@Docs\Camera Control PTP 3 Reference\Camera Control PTP 3 Reference.md` . 另外有一个基于MFC的GUI示例, 在 `\3rdparty\CamRmCmd_Demos\example-v3-windows`. 但是由于系统环境是MinGW, 且目标是提供易用的函数封装, 你应该尽可能规避其中依赖MSVC的部分, 只参考其中的API调用方式.
