<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/ubot-mark-light.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/brand/ubot-mark-dark.svg">
    <img alt="uBot" src="docs/brand/ubot-mark-dark.svg" height="88">
  </picture>
</p>
<h1 align="center">uBot Core</h1>
<p align="center"><b>uBot 插件家族共用的仿真时钟、帧头与插件管理。</b></p>
<p align="center">
  <a href="https://github.com/AstreoX/uBotCore/releases"><img alt="release" src="https://img.shields.io/github/v/release/AstreoX/uBotCore?label=release"></a>
  <a href="LICENSE"><img alt="license" src="https://img.shields.io/github/license/AstreoX/uBotCore"></a>
  <img alt="Unreal Engine 5.5" src="https://img.shields.io/badge/Unreal%20Engine-5.5-313131">
</p>
<p align="center">
  <a href="https://astreox.github.io/uBotDocs/core/">文档</a> ·
  <a href="https://github.com/AstreoX/uBotCore/releases">发布版本</a> ·
  <a href="https://github.com/users/AstreoX/projects/2">路线图</a> ·
  <a href="https://github.com/AstreoX/uBotCore/issues">问题反馈</a>
</p>

<div align="center">

[English](README.md) | **简体中文**

</div>

## 简介

uBot Core 是 uBot 插件家族的基础插件，面向 Unreal Engine 5.5。它放着所有 uBot 插件共用的部分，让它们在时间、坐标系和单位上保持一致。它只依赖 UE 引擎模块，不含 ROS 代码，目前是 Beta 版本。

- **统一帧头**：每个传感器样本都带传感器 ID、坐标系 ID、序号、时间戳和状态码。
- **统一仿真时钟**：所有 uBot 时间戳取自世界的游戏时间；可在蓝图或 C++ 中设置固定步长、暂停和单步。
- **UE 与 ROS 约定**：在 UE（左手系，厘米）与 ROS REP-103（右手系，米）之间转换位置、旋转、角速度、时间和名称。
- **共享接口**：传感器、执行器、指令和感知介质接口，让插件不必依赖彼此的类就能对接。
- **身份与采样**：`uBot Identity` 组件给机器人命名；采样调度器每帧最多触发一次。
- **插件管理**：Package 索引、依赖和版本校验、`Window > uBot` 面板；安装与更新由 uBot Manager 完成。

## 安装

**用 uBot Manager 安装（推荐）**，详细步骤见[安装教程](https://astreox.github.io/uBotDocs/start/install/)：

1. 从 [uBot Manager 的 Releases 页面](https://github.com/AstreoX/uBotManager/releases)下载安装包并运行。
2. 在 uBot Manager 中打开工程的 `.uproject`，选择 `UBotCore`，查看并应用更改；依赖会自动加入。
3. 用 Unreal Engine 5.5 打开工程，`Window > uBot` 面板的 Loaded Packages 里会列出 uBot Core 和它的版本。

**从源码安装**：把某个发布标签克隆到工程里，在 `Edit > Plugins` 中启用 *uBot Core*（分类 uBot）并重启编辑器；首次启动时编辑器会提示编译模块。

```shell
git clone --branch v0.1.2 https://github.com/AstreoX/uBotCore <Project>/Plugins/uBot/uBotCore
```

**要求**：Unreal Engine 5.5。从源码安装需要 git 和带 C++ 工具链的 Visual Studio 2022。uBot Manager 运行在 Windows 10 和 11 上，能用预编译包时优先使用预编译包。

## 快速开始

在屏幕上显示仿真时间。先把 `UBotCore` 加入模块的 `PublicDependencyModuleNames`，然后在 Actor 里：

```cpp
#include "Engine/Engine.h"
#include "UBotClock.h"

void AMyActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = UBot::Clock::GetSimTimeSeconds(this);
    GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::Green,
        FString::Printf(TEXT("%.3f"), Now));
}
```

`GetSimTimeSeconds` 返回世界的游戏时间，所以会跟随暂停和时间膨胀。在 Game 和 PIE 世界里，`UUBotClockSubsystem::Get(this)` 还提供 `PauseSimulation()`、`StepSimulation(1)` 和 `ResumeSimulation()`。蓝图里用 `Get UBotClockSubsystem` 和 `Get Sim Time Seconds`。

## 文档

使用手册在[文档站](https://astreox.github.io/uBotDocs/core/)。

| 我想… | 去这里 |
| --- | --- |
| 读取仿真时间、暂停和单步 | [仿真时钟](https://astreox.github.io/uBotDocs/core/clock/) |
| 在 UE 与 ROS 之间转换坐标、时间和名称 | [UE/ROS 约定库](https://astreox.github.io/uBotDocs/core/conventions/) |
| 看懂帧头和采样状态码 | [帧头与采样状态](https://astreox.github.io/uBotDocs/concepts/frame-header/) |
| 让传感器、执行器和指令在插件之间对接 | [插件之间怎么对接](https://astreox.github.io/uBotDocs/concepts/interfaces/) |
| 给机器人命名：ID、命名空间、基座坐标系 | [Identity 组件](https://astreox.github.io/uBotDocs/core/identity/) |
| 在面板、蓝图或命令行里查看已安装的 Package | [Package 查询与编辑器面板](https://astreox.github.io/uBotDocs/core/packages/) |
| 把自己的插件写成 uBot Package | [编写自己的 uBot Package](https://astreox.github.io/uBotDocs/develop/package-authoring/) |
| 在 C++ 里使用传感器与接口 | [在 C++ 中使用传感器与接口](https://astreox.github.io/uBotDocs/develop/cpp/) |
| 查 Package 索引、`UBot` 描述块、状态文件、扩展点 | [docs/PACKAGES.md](docs/PACKAGES.md) |
| 运行 `UBotPackage` 和 `UBotDocExport` commandlet | [docs/COMMANDLETS.md](docs/COMMANDLETS.md) |
| 查 C++ 的时钟、约定、采样调度和接口 API | [docs/CPP-API.md](docs/CPP-API.md) |
| 了解模块划分、运行测试 | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |

## uBot 家族

| 仓库 | 说明 |
| --- | --- |
| **[uBotCore](https://github.com/AstreoX/uBotCore)** | **仿真时钟、帧头、共享接口、插件管理** |
| [uBotSensor](https://github.com/AstreoX/uBotSensor) | 位姿、IMU、里程计、2D/3D 激光雷达、RGB 与深度相机 |
| [uBotROS](https://github.com/AstreoX/uBotROS) | 通过 TCP 桥接入 ROS 2：传感器、TF、/clock、cmd_vel |
| [uBotAerial](https://github.com/AstreoX/uBotAerial) | 多旋翼无人机：简化飞行、刚体动力学、风 |
| [uBotManager](https://github.com/AstreoX/uBotManager) | 在 UE 工程里安装、更新、构建 uBot 包的桌面程序和命令行 |
| [uBotDocs](https://github.com/AstreoX/uBotDocs) | 文档站：https://astreox.github.io/uBotDocs/ |

## 开发

- **构建**：把仓库克隆到 UE 5.5 C++ 工程的 `Plugins/uBot/uBotCore`；首次启动时，编辑器会提示编译模块。
- **测试**：自动化测试以 `UBotCore.` 开头。在编辑器里打开 `Tools > Session Frontend`，切到 Automation 标签页，过滤 `UBotCore`；命令行运行见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。
- **反馈问题**：在 [GitHub Issues](https://github.com/AstreoX/uBotCore/issues) 提交，请附上 uBot Core 的版本和编辑器日志里的 `LogUBot` 行。

## 许可证

[Apache License 2.0](LICENSE)，另见 [NOTICE](NOTICE)。Copyright 2026 AstreoX。可以自由使用、修改和再分发（包括商业用途）；再分发时需保留 `LICENSE` 与 `NOTICE` 文件，并注明你修改过的文件。
