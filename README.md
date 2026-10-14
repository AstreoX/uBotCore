<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/ubot-mark-light.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/brand/ubot-mark-dark.svg">
    <img alt="uBot" src="docs/brand/ubot-mark-dark.svg" height="88">
  </picture>
</p>
<h1 align="center">uBot Core</h1>
<p align="center"><b>Shared simulation clock, frame header and package management for the uBot plugin family.</b></p>
<p align="center">
  <a href="https://github.com/AstreoX/uBotCore/releases"><img alt="release" src="https://img.shields.io/github/v/release/AstreoX/uBotCore?label=release"></a>
  <a href="LICENSE"><img alt="license" src="https://img.shields.io/github/license/AstreoX/uBotCore"></a>
  <img alt="Unreal Engine 5.5" src="https://img.shields.io/badge/Unreal%20Engine-5.5-313131">
</p>
<p align="center">
  <a href="https://astreox.github.io/uBotDocs/core/">Documentation</a> ·
  <a href="https://github.com/AstreoX/uBotCore/releases">Releases</a> ·
  <a href="https://github.com/users/AstreoX/projects/2">Roadmap</a> ·
  <a href="https://github.com/AstreoX/uBotCore/issues">Issues</a>
</p>

<div align="center">

**English** | [简体中文](README.zh-CN.md)

</div>

## What it is

uBot Core is the foundation plugin of the uBot family for Unreal Engine 5.5. It holds what all uBot plugins share, so they agree on time, frames and units. It depends only on Unreal Engine modules, contains no ROS code, and is currently a beta release.

- **One frame header.** Every sensor sample carries a sensor id, frame id, sequence number, timestamp and status code.
- **One simulation clock.** All uBot timestamps use the world's game time; set a fixed time step, pause and single-step from Blueprints or C++.
- **UE and ROS conventions.** Convert positions, rotations, angular velocity, time and names between Unreal (left-handed, cm) and ROS REP-103 (right-handed, m).
- **Shared interfaces.** Sensor, actuator, command and perception-medium interfaces connect plugins without depending on each other's classes.
- **Identity and sampling.** The `uBot Identity` component names a robot; the sample scheduler fires at most once per frame.
- **Package management.** Package index, dependency and version checks and the `Window > uBot` panel; uBot Manager installs and updates packages.

## Install

**With uBot Manager (recommended)** — [step-by-step guide](https://astreox.github.io/uBotDocs/start/install/)

1. Download the installer from the [uBot Manager releases](https://github.com/AstreoX/uBotManager/releases) and run it.
2. Open your project's `.uproject` in uBot Manager, select `UBotCore`, then review and apply the changes. Dependencies are added automatically.
3. Open the project in Unreal Engine 5.5. `Window > uBot` lists uBot Core and its version under Loaded Packages.

**From source:** clone a release tag into your project, enable *uBot Core* (category uBot) in `Edit > Plugins` and restart the editor. On first launch the editor offers to compile the modules.

```shell
git clone --branch v0.1.2 https://github.com/AstreoX/uBotCore <Project>/Plugins/uBot/uBotCore
```

**Requirements:** Unreal Engine 5.5. Source installs need git and Visual Studio 2022 with the C++ toolchain. uBot Manager runs on Windows 10 and 11 and uses prebuilt packages where it can.

## Quick start

Show the simulation time on screen. Add `UBotCore` to your module's `PublicDependencyModuleNames`, then in an Actor:

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

`GetSimTimeSeconds` returns the world's game time, so it follows pause and time dilation. In Game and PIE worlds, `UUBotClockSubsystem::Get(this)` also gives `PauseSimulation()`, `StepSimulation(1)` and `ResumeSimulation()`. In Blueprints, use `Get UBotClockSubsystem` and `Get Sim Time Seconds`.

## Documentation

The manual is on the [documentation site](https://astreox.github.io/uBotDocs/core/). The site and the Markdown files in `docs/` are written in Chinese.

| I want to ... | Go to |
| --- | --- |
| Read the simulation time, pause or single-step | [Simulation clock](https://astreox.github.io/uBotDocs/core/clock/) |
| Convert coordinates, time and names between UE and ROS | [UE/ROS conventions](https://astreox.github.io/uBotDocs/core/conventions/) |
| Read frame headers and sample status codes | [Frame header and sample status](https://astreox.github.io/uBotDocs/concepts/frame-header/) |
| Connect sensors, actuators and commands across plugins | [Interfaces between plugins](https://astreox.github.io/uBotDocs/concepts/interfaces/) |
| Name a robot: id, namespace, base frame | [Identity component](https://astreox.github.io/uBotDocs/core/identity/) |
| See installed packages in the panel, Blueprints or CLI | [Package query and editor panel](https://astreox.github.io/uBotDocs/core/packages/) |
| Turn my own plugin into a uBot Package | [Writing a uBot Package](https://astreox.github.io/uBotDocs/develop/package-authoring/) |
| Use sensors and interfaces from C++ | [Using uBot from C++](https://astreox.github.io/uBotDocs/develop/cpp/) |
| Look up the package index, `UBot` block, status file, extension points | [docs/PACKAGES.md](docs/PACKAGES.md) |
| Run the `UBotPackage` and `UBotDocExport` commandlets | [docs/COMMANDLETS.md](docs/COMMANDLETS.md) |
| Look up the C++ clock, conventions, scheduler and interface APIs | [docs/CPP-API.md](docs/CPP-API.md) |
| See the module layout and run the tests | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |

## uBot family

| Repository | What it is |
| --- | --- |
| **[uBotCore](https://github.com/AstreoX/uBotCore)** | **Simulation clock, frame header, shared interfaces, package management** |
| [uBotSensor](https://github.com/AstreoX/uBotSensor) | Pose, IMU, odometry, 2D/3D lidar, RGB and depth cameras |
| [uBotROS](https://github.com/AstreoX/uBotROS) | ROS 2 over a TCP bridge: sensors, TF, /clock, cmd_vel |
| [uBotAerial](https://github.com/AstreoX/uBotAerial) | Multirotor drones: simplified flight, rigid-body dynamics, wind |
| [uBotManager](https://github.com/AstreoX/uBotManager) | Desktop app and CLI to install, update and build uBot packages |
| [uBotDocs](https://github.com/AstreoX/uBotDocs) | Documentation site: https://astreox.github.io/uBotDocs/ |

## Development

- **Build:** clone into a UE 5.5 C++ project under `Plugins/uBot/uBotCore`; the editor offers to compile the modules on first launch.
- **Test:** the Automation tests are named `UBotCore.*`. In the editor open `Tools > Session Frontend`, switch to the Automation tab and filter by `UBotCore`. The command-line run is in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- **Report an issue:** open one on [GitHub](https://github.com/AstreoX/uBotCore/issues) with the uBot Core version and the `LogUBot` lines from the editor log.

## License

[Apache License 2.0](LICENSE), see also [NOTICE](NOTICE). Copyright 2026 AstreoX. You may use, modify and redistribute it, including commercially; keep the `LICENSE` and `NOTICE` files and mark the files you changed.
