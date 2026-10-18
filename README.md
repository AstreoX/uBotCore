<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/ubot-mark-light.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/brand/ubot-mark-dark.svg">
    <img alt="uBot" src="docs/brand/ubot-mark-dark.svg" height="88">
  </picture>
</p>
<h1 align="center">uBot Core</h1>
<p align="center"><b>Core of the uBot plugin family: unified frame header, simulation clock and package management.</b></p>
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

uBot Core is the foundation plugin of the uBot family for Unreal Engine 5.5. It provides the components that all uBot plugins share, so that they use the same time base, coordinate conventions and units.

- **Frame header.** Every sensor sample carries a sensor id, frame id, sequence number, timestamp and status code.
- **Simulation clock.** All uBot timestamps use the world's game time. Fixed time step, pause and single-step are available from Blueprints and C++.
- **Coordinate conventions.** Conversion of positions, rotations, angular velocity and transforms between Unreal (left-handed, cm) and right-handed, Z-up SI frames such as ROS REP-103 or MuJoCo.
- **Shared interfaces.** Sensor, actuator, command and perception-medium interfaces connect plugins without class dependencies between them.
- **Wind queries.** `UUBotWindQuerySubsystem` asks an optional `IUBotWindProvider` in the same world for wind in UE world axes (m/s). Consumers depend only on Core; without a provider, queries return zero. Position is in centimeters and time is uBot simulation seconds. Registration and queries run on the game thread.
- **Identity and sampling.** The `uBot Identity` component names a robot; the sample scheduler fires at most once per frame.
- **Package management.** Package index, dependency and version checks, and the `Window > uBot` panel. uBot Manager installs and updates packages.

## Install

**With uBot Manager (recommended)**, see the [step-by-step guide](https://astreox.github.io/uBotDocs/start/install/):

1. Download the installer from the [uBot Manager releases](https://github.com/AstreoX/uBotManager/releases) and run it.
2. Open the project's `.uproject` in uBot Manager, select `UBotCore`, then review and apply the changes. Dependencies are resolved automatically.
3. Open the project in Unreal Engine 5.5. The Loaded Packages section of `Window > uBot` lists uBot Core and its version.

**From source:** clone a release tag into the project, enable *uBot Core* (category uBot) under `Edit > Plugins` and restart the editor. The editor prompts to compile the modules on first launch.

```shell
git clone --branch v0.1.2 https://github.com/AstreoX/uBotCore <Project>/Plugins/uBot/uBotCore
```

**Requirements:** Unreal Engine 5.5. A source install requires git and Visual Studio 2022 with the C++ toolchain. uBot Manager runs on Windows 10 and 11 and installs prebuilt packages where available.

## Documentation

The manual is published on the [documentation site](https://astreox.github.io/uBotDocs/core/). The site and the Markdown files in `docs/` are written in Chinese.

| I want to ... | Go to |
| --- | --- |
| Read the simulation time; pause and single-step | [Simulation clock](https://astreox.github.io/uBotDocs/core/clock/) |
| Convert coordinates between UE and right-handed SI frames | [Coordinate conventions](https://astreox.github.io/uBotDocs/core/conventions/) |
| Interpret frame headers and sample status codes | [Frame header and sample status](https://astreox.github.io/uBotDocs/concepts/frame-header/) |
| Connect sensors, actuators and commands across plugins | [Interfaces between plugins](https://astreox.github.io/uBotDocs/concepts/interfaces/) |
| Name a robot (id, namespace, base frame) | [Identity component](https://astreox.github.io/uBotDocs/core/identity/) |
| Inspect installed packages (panel, Blueprints, command line) | [Package query and editor panel](https://astreox.github.io/uBotDocs/core/packages/) |
| Write my own uBot Package | [Writing a uBot Package](https://astreox.github.io/uBotDocs/develop/package-authoring/) |
| Use sensors and interfaces from C++ | [Using uBot from C++](https://astreox.github.io/uBotDocs/develop/cpp/) |
| Look up the package index, `UBot` descriptor block, status file and extension points | [docs/PACKAGES.md](docs/PACKAGES.md) |
| Run the `UBotPackage` and `UBotDocExport` commandlets | [docs/COMMANDLETS.md](docs/COMMANDLETS.md) |
| Look up the C++ API for clock, conventions, scheduler and interfaces | [docs/CPP-API.md](docs/CPP-API.md) |
| Find the module layout and the test instructions | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |

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

- **Build:** clone into the `Plugins/uBot/uBotCore` folder of a UE 5.5 C++ project. The editor prompts to compile the modules on first launch.
- **Test:** the Automation tests are named `UBotCore.*`. Open `Tools > Session Frontend`, select the Automation tab and filter by `UBotCore`. The command-line invocation is in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
- **Issues:** report them on [GitHub Issues](https://github.com/AstreoX/uBotCore/issues), stating the uBot Core version and the relevant `LogUBot` lines from the editor log.

## License

[Apache License 2.0](LICENSE), see also [NOTICE](NOTICE). Copyright 2026 AstreoX. The software may be used, modified and redistributed, including commercially, provided that the `LICENSE` and `NOTICE` files are kept and modified files are marked.
