# uBot Core

uBot 家族的基础层插件：提供统一的传感器帧头与状态码、仿真时钟、UE 与 ROS 的坐标约定、采样调度器、传感器/执行器/感知介质接口、机器人身份组件，以及 uBot 包管理器（依赖与版本校验、包索引、编辑器窗口、命令行）。

## 在 uBot 家族中的位置

uBotCore 是 **Foundation** 层，只依赖 UE 引擎模块，不包含任何 ROS 头文件或 rclcpp。上层包以它为基础：

| 包 | 层 | 依赖 | 内容 |
| --- | --- | --- | --- |
| `UBotCore` | Foundation | 仅引擎模块 | 本插件 |
| `UBotSensor` | Capability | `UBotCore ^0.1.0` | 位姿、里程计、IMU、2D/3D 激光雷达、RGB/深度相机、烟雾等感知介质 |
| `UBotROS` | Adapter | `UBotCore ^0.1.0`、`UBotSensor ^0.1.0` | 通过自带的轻量 TCP 桥（ubot_ros_bridge）发布传感器、TF 与 /clock，接收速度指令 |

以上关系来自内置包索引 `Resources/PackageIndex.json`。

| 模块 | 类型 | 加载阶段 | 说明 |
| --- | --- | --- | --- |
| `UBotCore` | Runtime | PreDefault | 运行时 API |
| `UBotCoreEditor` | Editor | Default | uBot Packages 窗口与 `UBotPackage` commandlet |

## 安装

要求 UE 5.5 或更高版本。插件以源码形式分发，工程需要能编译 C++（首次打开工程时编辑器会提示编译模块）。

1. 把仓库克隆到 `<Project>/Plugins/uBot/uBotCore`：

   ```powershell
   git clone https://github.com/AstreoX/uBotCore.git <Project>/Plugins/uBot/uBotCore
   ```

2. 启用插件：在编辑器的 `Edit > Plugins` 中搜索 uBot Core（分类 uBot）并勾选；或在 `.uproject` 中加入

   ```json
   "Plugins": [ { "Name": "UBotCore", "Enabled": true } ]
   ```

3. 重启编辑器。设置位于 `Project Settings > Plugins > uBot Core`，保存在 `Config/DefaultEngine.ini` 的 `[/Script/UBotCore.UBotCoreSettings]` 段。

## 功能

### 帧头与状态码

`FUBotFrameHeader`（`UBotTypes.h`）是所有 uBot 传感器帧共用的帧头：

| 字段 | 说明 |
| --- | --- |
| `SensorId` | 传感器 ID |
| `FrameId` | 传感器所在的 TF frame id |
| `Sequence` | `int64` 帧序号 |
| `TimestampSeconds` | 取样时的 uBot 仿真时间 |
| `bValid` | 本帧是否有效 |
| `Status` | `EUBotSampleStatus` |

`EUBotSampleStatus`：`Valid`、`WarmingUp`、`InvalidDeltaTime`、`TeleportDetected`、`Disabled`、`Error`。字段名和枚举顺序与旧版 `FAgentSensorFrameHeader` / `EAgentSensorSampleStatus` 一致，因此迁移时 struct CoreRedirect 不需要属性重定向。

### 仿真时钟与单步

所有 uBot 时间戳使用同一个时间基准：`UWorld::GetTimeSeconds()`（游戏时间，遵循暂停和时间膨胀）。

```cpp
#include "UBotClock.h"

const double Now = UBot::Clock::GetSimTimeSeconds(this); // 没有 World 时返回 0
```

`UUBotClockSubsystem`（`UTickableWorldSubsystem`，只为 Game / PIE 世界创建）提供下面的控制，蓝图类别为 `uBot|Clock`：

| 函数 | 说明 |
| --- | --- |
| `UUBotClockSubsystem::Get(WorldContext)` | C++ 静态入口，可能返回 `nullptr` |
| `GetSimTimeSeconds()` | 与 `UBot::Clock::GetSimTimeSeconds` 相同 |
| `GetSimFrame()` | 游戏时间前进过的 world tick 数；暂停时冻结，因此同一帧号不会对应两个不同的仿真时间 |
| `SetFixedTimeStep(bEnabled, StepHz = 60)` / `IsFixedTimeStepEnabled()` | 开关引擎固定步长（`FApp`，进程全局）。`StepHz` 限制在 [1, 1000]；第一次修改时记录原值，最后一个修改过它的时钟子系统销毁时恢复 |
| `PauseSimulation()` / `ResumeSimulation()` / `IsSimulationPaused()` | 通过 `UGameplayStatics::SetGamePaused` 暂停与恢复，并取消未完成的单步 |
| `StepSimulation(NumFrames = 1)` | 恢复运行恰好 `NumFrames` 个 world tick 后再暂停。`NumFrames < 1` 或无法取消暂停时返回 `false`；单步进行中再次调用会累加剩余帧数 |
| `GetRemainingStepFrames()` | 剩余单步帧数 |

说明：

- 暂停依赖本地 PlayerController 且 GameMode 允许暂停；编辑器自己的 PIE 暂停会让世界一直保持暂停，此时恢复也不会成功。暂停或恢复没有达到目标状态时，日志里会有 `LogUBot` 警告。
- 子系统在暂停时仍会 tick，这样单步才能知道世界何时重新运行。
- Project Settings 的 `Clock` 分类：`bUseFixedTimeStep` 开启后，每个 Game / PIE 世界 `BeginPlay` 时按 `FixedTimeStepHz`（1 到 1000）启用固定步长。关闭它不会强制关闭已有的固定步长。
- 在不支持固定步长的构建配置中调用 `SetFixedTimeStep(true, ...)` 会记录警告且不生效。

### UE 与 ROS 坐标约定

UE 是左手系（X 前、Y 右、Z 上，单位厘米），ROS 遵循 REP-103（右手系，X 前、Y 左、Z 上，单位米）。两者的映射是镜像 `M = diag(1, -1, 1)`，所有映射都是自逆的，ROSToUE 与 UEToROS 公式相同（仅单位换算不同）。

`UBotRosConventions.h` 的 `UBot::Ros` 命名空间是纯数学函数，不含 ROS 类型；同一组函数也以蓝图节点的形式提供（`UUBotConventionsLibrary`，类别 `uBot|Conventions`）。

| 量 | UE → ROS | `UBot::Ros` 函数（反向为 `ROSToUE`） | 蓝图节点（`UE To ROS …` / `ROS To UE …`） |
| --- | --- | --- | --- |
| 位置 | `(x, -y, z) / 100`（cm → m） | `PositionUEToROS` | `… Position` |
| 线性量（线速度、加速度、已是米的点） | `(x, -y, z)` | `VectorUEToROS` | `… Vector` |
| 轴向量（角速度、力矩） | `(-x, y, -z)` | `AngularVelocityUEToROS` | `… Angular Velocity` |
| 旋转四元数 | `(x, y, z, w)` → 归一化的 `(-x, y, -z, w)` | `RotationUEToROS` | `… Rotation` |
| 刚体变换 | 旋转镜像，平移 cm → m，缩放丢弃（输出缩放为 1） | `TransformUEToROS` | `… Transform` |
| 航向、扫描角、偏航角速度 | `ROS = -UE` | `YawUEToROS` | `… Yaw` |

其他函数：

| 函数 | 说明 |
| --- | --- |
| `CameraLinkToOpticalRotation()` | 相机光学系（z 前、x 右、y 下）相对相机 link 系的旋转，ROS 约定下为 `(x=-0.5, y=0.5, z=-0.5, w=0.5)`。蓝图节点 `Camera Link To Optical Rotation` |
| `OpticalFrameSuffix()` | 光学系 frame id 的后缀 `"_optical"`。蓝图节点 `Get Optical Frame Suffix` |
| `SecondsToRosTime` / `RosTimeToSeconds` | 秒与 `builtin_interfaces/Time` 的 `(sec, nanosec)` 互转。负数或非有限值得到 `(0, 0)`；纳秒四舍五入，进位进入秒。蓝图节点 `Seconds To ROS Time`、`ROS Time To Seconds`（纳秒为 `int32`） |
| `SanitizeRosName` | 只保留 `[A-Za-z0-9_]`，其余替换为 `_`，合并重复下划线，首字符为数字时加前缀 `_`，结果为空时返回 `"ubot"`。蓝图节点 `Sanitize ROS Name` |

`UBotUnits.h` 里还有只含头文件的 `UBot::Units`：`CentimetersToMeters`、`MetersToCentimeters`（标量与 `FVector`）、`DegreesToRadians`、`RadiansToDegrees`。uBot 公共 API 默认使用 SI（米、秒、弧度），名字中带 `Centimeters` 或 `Degrees` 的除外。

### 采样调度器

`FUBotSampleScheduler`（`UBotSampleScheduler.h`）取代传感器常用的循环 `FTimerManager` 定时器：当一帧耗时超过采样间隔时，循环定时器会在同一帧触发多次，产生重复时间戳；调度器每次轮询最多触发一次。

```cpp
FUBotSampleScheduler Scheduler;
Scheduler.Reset(30.0, UBot::Clock::GetSimTimeSeconds(this)); // BeginPlay：30 Hz，立即到期

// 每个 tick：
if (Scheduler.ConsumeDue(UBot::Clock::GetSimTimeSeconds(this)))
{
    // 取一个样本；GetDroppedSampleCount() 给出因掉帧而跳过的采样间隔数
}
```

- `Reset(RateHz, FirstDueSeconds)`：频率限制在 [0.01, 10000] Hz，清零丢弃计数；`FirstDueSeconds` 不是有限数时调度器保持未初始化。
- `SetRateHz(RateHz)`：保持相位，下一次到期时间不变。
- `ConsumeDue(NowSeconds)`：到期返回 `true`，并把下一次到期时间推进到严格大于 `NowSeconds` 的网格点；多跳过的每个间隔计入丢弃数（0.5 s 的单帧在 100 Hz 下触发一次，丢弃 49 个）。`NowSeconds` 小于下一次到期时间、不是有限数，或调度器未初始化时返回 `false`。
- 到期时间按网格 `FirstDue + k / RateHz` 由序号计算，不累加，长时间运行不会漂移。
- 其他访问器：`GetRateHz`、`GetIntervalSeconds`、`GetNextDueSeconds`、`GetDroppedSampleCount`、`IsInitialized`。

### 接口

四个接口都是 `UINTERFACE(BlueprintType)`，函数为 `BlueprintNativeEvent`，C++ 组件和蓝图 Actor 都可以实现。调用方先 `Object->Implements<UUBotX>()`，再用 `IUBotX::Execute_Fn(Object, ...)`，这样原生与蓝图实现都能走到。

| 接口 | 头文件 | 函数 |
| --- | --- | --- |
| `IUBotSensor` | `UBotSensorInterface.h` | `GetSensorId`、`GetSensorFrameId`、`GetSensorType`（短而稳定的类型键，如 `Pose`、`IMU`、`Lidar2D`）、`IsSensorRunning`、`GetLatestSensorHeader`（`FUBotFrameHeader`）、`GetSensorFrameComponent`（作为 TF frame 的场景组件，可为空） |
| `IUBotActuator` | `UBotActuatorInterface.h` | `GetActuatorId`、`GetActuatorType`、`IsActuatorReady` |
| `IUBotVelocityCommandable` | `UBotActuatorInterface.h` | `ApplyVelocityCommand(const FUBotVelocityCommand&)`（被拒绝时返回 `false`）、`StopMotion`。独立的能力接口，不要求同时实现 `IUBotActuator` |
| `IUBotPerceptionMedium` | `UBotPerceptionMedium.h` | `GetMediumSnapshot(FUBotMediumSnapshot&)` |

查找辅助函数：

- `UBot::Sensors::FindSensors(Actor, OutSensors)`：返回 Actor 自身（若实现接口）加上所有实现 `IUBotSensor` 的组件，按组件顺序。
- `UBot::Actuation::FindVelocityCommandable(Actor)`：先 Actor 本身，再其组件，没有则 `nullptr`。
- `UBot::Interfaces::FindImplementers` / `FindFirstImplementer`：以上两者使用的通用版本，接受任意接口的 `UClass`。不搜索子 Actor 组件。

```cpp
TArray<UObject*> Sensors;
UBot::Sensors::FindSensors(Robot, Sensors);
for (UObject* Sensor : Sensors)
{
    const FString Id = IUBotSensor::Execute_GetSensorId(Sensor);
}

if (UObject* Drive = UBot::Actuation::FindVelocityCommandable(Robot))
{
    FUBotVelocityCommand Command;
    Command.LinearVelocityMetersPerSecond = FVector(0.5, 0.0, 0.0);
    Command.TimestampSeconds = UBot::Clock::GetSimTimeSeconds(Robot);
    Command.Source = TEXT("MCP");
    IUBotVelocityCommandable::Execute_ApplyVelocityCommand(Drive, Command);
}
```

`FUBotVelocityCommand` 在机体系下、使用 UE 坐标轴（X 前、Y 右、Z 上）：线速度单位 m/s，角速度单位 rad/s 且采用 UE 旋转方向（+Z 为向右偏航）。`Source` 是自由格式的来源标签，如 `ROS`、`MCP`、`Blueprint`。其他坐标系的生产者（例如 ROS 适配器）负责先转换再填充。

感知介质（烟、雾等）由球形单元组成：

- `FUBotMediumCell`：`WorldPositionCentimeters`、`RadiusCentimeters`（小于 1 cm 按 1 cm）、`Density`（0 到 1）、`EdgeFactor`（0 到 1，向介质边缘衰减密度）。
- `FUBotMediumSnapshot`：`MediumType`（默认 `Smoke`）、`bActive`、`Tint`、`Cells`，以及非 UPROPERTY 的 `BoundsCentimeters` 和 `Source`。修改 `Cells` 后需要再次调用 `FinalizeSnapshot`，否则包围盒过期会让相交测试漏掉单元。
- `UBot::Medium`：`EffectiveDensity(Cell)`（`Density * EdgeFactor`，限制在 0 到 1）、`FinalizeSnapshot`、`CollectSnapshots(World, Out)`（收集世界中所有实现了 `IUBotPerceptionMedium` 且处于激活并非空的快照）、`FindFirstIntersection(Start, End, Snapshots, DensityThreshold, OutDistance, OutDensity)`（线段上第一个进入有效密度不低于阈值的单元的位置，距离从起点量起，单位厘米；起点已在单元内时为 0）。

### 身份组件

`UUBotIdentityComponent`（Add Component 中的 **uBot Identity**，类别 uBot）为 ROS 桥、MCP 等所有 uBot 使用方命名机器人。全部字段可选，解析函数负责回退：

| 字段 | 默认 | 解析规则 |
| --- | --- | --- |
| `RobotId` | 空 | 空则使用所属 Actor 的名字 |
| `Namespace` | 空 | 空则使用 `SanitizeRosName(机器人 ID)`；首尾空白与首尾 `/` 会被去掉 |
| `BaseFrameId` | `base_link` | 空则回退为 `base_link` |
| `BaseComponent` | 空 | 空、无法解析或不是场景组件时使用 Actor 的根组件 |

蓝图节点：`GetResolvedRobotId`、`GetResolvedNamespace`、`GetBaseFrameId`、`GetBaseComponent`（类别 `uBot|Identity`）。

C++ 的 `UBot::Identity` 提供 `Find(Actor)`、`ResolveRobotId`、`ResolveNamespace`、`ResolveBaseFrameId`、`ResolveBaseComponent`：规则与组件一致，Actor 没有该组件时回退到 Actor 名、净化后的 Actor 名、`base_link` 和根组件。名字取自 `AActor::GetName()` 而不是编辑器标签，所以打包后结果相同。

## 包管理

uBot 包就是 `.uplugin` 带有顶层 `"UBot"` 对象的 UE 插件。版本取自原生的 `VersionName`（语义化版本）。原生的 `"Plugins"` 依赖仍然是 UE 强制执行的机制，`"UBot"` 块在其上补充版本范围、层级和目录元数据。

### `.uplugin` 中的 `"UBot"` 块

```json
{
    "FileVersion": 3,
    "VersionName": "0.1.0",
    "FriendlyName": "uBot Sensor",
    "Description": "...",
    "DocsURL": "https://github.com/AstreoX/uBotSensor",
    "Plugins": [ { "Name": "UBotCore", "Enabled": true } ],
    "UBot": {
        "Layer": "Capability",
        "Repository": "https://github.com/AstreoX/uBotSensor.git",
        "Requires": [
            { "Name": "UBotCore", "Version": "^0.1.0" },
            { "Name": "SomeOptional", "Version": ">=1.0", "Optional": true }
        ],
        "Provides": [ "Sensor.IMU" ],
        "Tags": [ "sensor" ]
    }
}
```

| 字段 | 说明 |
| --- | --- |
| `VersionName`（原生） | 包版本，应为语义化版本；缺失或无法解析会记为描述符问题 |
| `FriendlyName`、`Description`、`DocsURL`（原生） | 显示信息；`FriendlyName` 缺省为插件名 |
| `Layer` | `Foundation`、`Capability`、`Composition`、`Adapter`、`Content`（不区分大小写）；缺失或无法识别记为描述符问题 |
| `Repository` | 安装与更新使用的 git 仓库 URL |
| `Requires` | 依赖列表，每项含 `Name`、`Version`（版本约束，缺省为任意版本）、`Optional`（默认 `false`） |
| `Provides` | 本包提供的能力键，如 `Sensor.IMU` |
| `Tags` | 标签 |
| `ExternalRequires`（可选） | 需要的非 uBot 插件（例如某个第三方 UE 插件），仅作提示，不会被自动安装 |
| `DocsUrl`（可选） | 覆盖原生 `DocsURL` |

只有存在 `"UBot"` 对象的插件才会被当作 uBot 包。可选依赖（`Optional: true`）只在对方已安装但版本不满足时才算问题，且不构成依赖边。

### 版本约束语法

`Requires[].Version` 和蓝图/C++ 中的约束字符串共用同一套语法（`FUBotVersionConstraint`）。版本号接受 `1`、`1.2`、`1.2.3`，可带前缀 `v` 和 `-prerelease`，`+build` 元数据被忽略。多个比较器用空格分隔，必须全部满足；运算符与版本之间也可以有空格（`>= 1.0`）。

| 写法 | 含义 |
| --- | --- |
| 空字符串、`*` | 任意版本 |
| `1.2.3`、`=1.2.3` | 恰好等于 1.2.3 |
| `>=1.2.3`、`>1.2.3`、`<=1.2.3`、`<1.2.3` | 比较 |
| `^1.2.3` | `>=1.2.3 <2.0.0` |
| `^0.2.3` | `>=0.2.3 <0.3.0` |
| `^0.0.3` | `>=0.0.3 <0.0.4` |
| `~1.2.3` | `>=1.2.3 <1.3.0` |
| `~1` | `>=1.0.0 <2.0.0` |
| `1.2`、`=1.2` | 部分版本按 X-range：`>=1.2.0 <1.3.0` |
| `>=1.0.0 <2.0.0` | 区间：所有比较器同时成立 |

预发布版本按 SemVer 2.0 排序参与比较；但由 `^`、`~` 和部分版本产生的上界会排除该上界本身的预发布版本，例如 `^1.2.3` 不接受 `2.0.0-beta`。

### 校验

`FUBotPackageRegistry::ValidatePackageSet` 为每个包计算问题列表：

- 必需的包不存在或未安装
- 已安装的依赖不满足版本约束
- 已启用的包依赖了未启用的包
- 版本约束无法解析
- 依赖循环

默认在所有模块加载完成后做一次校验（`bValidatePackagesOnStartup`），每个问题以 `LogUBot` 警告输出，并以 `uBot packages: N installed, M enabled, K problem(s).` 收尾；也会提示注册在从未声明的扩展点上的扩展。问题同样显示在 uBot Packages 窗口的 Problems 列和详情中，以及 commandlet 的 `-Validate` 中。

### 项目设置

`Project Settings > Plugins > uBot Core` 的 `Packages` 分类：

| 设置 | 默认 | 说明 |
| --- | --- | --- |
| `bValidatePackagesOnStartup` | `true` | 启动后校验已安装的包并记录所有问题 |
| `PackageInstallDirectory` | `Plugins/uBot` | 新包的安装目录，相对工程目录 |
| `AdditionalPackageIndexFiles` | 空 | 额外的包索引 JSON 文件（绝对路径或相对工程目录） |
| `GitExecutable` | `git` | 安装与更新使用的 git 可执行文件，不能是 shell 或批处理脚本 |

### 包索引

包索引是本地 JSON 文件，列出已知但可能尚未安装的包，供包管理器显示并安装。格式：

```json
{
    "FormatVersion": 1,
    "Packages": [
        {
            "Name": "UBotSonar",
            "FriendlyName": "uBot Sonar",
            "Description": "Simulated sonar sensor.",
            "Layer": "Capability",
            "Repository": "https://example.com/team/uBotSonar.git",
            "DocsUrl": "https://example.com/team/uBotSonar",
            "Version": "0.2.0",
            "Tags": [ "sensor" ],
            "Provides": [ "Sensor.Sonar" ],
            "Requires": [
                { "Name": "UBotCore", "Version": "^0.1.0" },
                { "Name": "UBotSensor", "Version": "^0.1.0" }
            ],
            "ExternalRequires": []
        }
    ]
}
```

只有 `Name` 是必填项；`Version` 是已知最新版本；安装一个包至少需要 `Repository`。`FormatVersion` 存在时必须为 `1`。损坏的条目（不是对象、没有 `Name`、重名）会被跳过并报告，其余条目照常加载。

**添加自己的索引**：在 `Project Settings > Plugins > uBot Core > Packages > Additional Package Index Files` 中添加文件路径，或直接写入 `Config/DefaultEngine.ini`：

```ini
[/Script/UBotCore.UBotCoreSettings]
+AdditionalPackageIndexFiles=Config/uBotPackages.json
```

加载顺序：插件自带的 `Resources/PackageIndex.json`（列出 UBotCore、UBotSensor、UBotROS）始终最先加载，然后按设置顺序加载额外文件；同名条目由后加载的覆盖，位置保持不变。读取失败的文件会在窗口消息区或日志中报告，不会影响其他文件。

索引与已安装的包合并后得到包视图：已安装的包以其自身 `.uplugin` 的数据为准，只在 `Repository` / `DocsUrl` 为空时用索引补齐；只出现在索引里的包显示为 Not installed。

### uBot Packages 窗口

从 **Window** 菜单或 **Tools** 菜单（两者都在 uBot 分区下）打开 **uBot Packages**（可停靠的 nomad 标签页 `UBotPackageManager`）。

- 工具栏：**Refresh** 重新扫描已安装的插件并重新加载索引文件；**Restart Editor** 在需要重启时可用，确认后重启编辑器；状态文本；有 git 任务运行时显示进度圈和 **Cancel**（终止 git，并清理被中断的克隆留下的半成品目录）。
- 列表列：Name、Version、Layer、State、Problems（问题数量，悬停可看详情）。State 为 Not installed、Enabled、Disabled，或刚改过项目文件但尚未重启时的 Enabled after restart / Disabled after restart。
- 详情面板：版本、层级、状态、位置、描述、仓库、文档、标签、Provides、Requires、外部插件、该包拥有的扩展点和扩展、问题。
- 消息区：记录每次操作的输出和索引错误。

详情下方的按钮：

| 按钮 | 作用 |
| --- | --- |
| **Install** | 仅对索引中有 `Repository` 且未安装的包可用，确认后执行。先按依赖顺序对缺失的必需依赖执行 `git clone -- <Repository> <工程目录>/<PackageInstallDirectory>/<仓库文件夹名>`，再克隆该包本身；文件夹名是仓库 URL 最后一段去掉 `.git`。每次克隆后用 `IPluginManager::AddToPluginsList` 注册，全部完成后刷新注册表并启用该包。目标文件夹已存在时拒绝；仓库根目录必须有 `<包名>.uplugin`。`ExternalRequires` 不会安装，只给出提示 |
| **Update** | 对安装目录是 git 工作副本的包可用，执行 `git -C <BaseDir> pull --ff-only`。有变化时需要重新编译并重启编辑器 |
| **Enable** | 启用该包及其所有必需依赖（依赖优先），通过 `IProjectManager::SetPluginEnabled` 写入并保存 `.uproject`。必需依赖未安装时不做任何修改 |
| **Disable** | 在 `.uproject` 中禁用该包。有已启用的包依赖它时，会列出这些包并在确认后把它们（依赖者优先）一并禁用 |
| **Open Folder** | 在文件管理器中打开包所在目录 |
| **Open Repository** | 在浏览器中打开仓库地址（仅 http/https 地址） |

启用、禁用和安装只修改项目文件，要重启编辑器后才生效；新克隆的包带有 C++ 源码，重启时编辑器会提示编译缺失的模块（需要 C++ 工程与编译器）。

git 的运行方式：不经过 shell，参数逐个加引号；仓库地址只取自包索引，且只允许 `https`、`http`、`ssh`、`git`、`file` 协议及 scp 形式（拒绝以 `-` 开头的地址和 `<transport>::` 形式的 remote helper）；输出和日志中的 URL 凭据会被替换为 `***`；设置了 `GIT_TERMINAL_PROMPT=0`，所以私有仓库需要事先配置好 SSH key 或 credential helper，git 不会弹出交互式提示。

### 命令行（commandlet）

`UBotCoreEditor` 提供 `UBotPackage` commandlet，无需 Slate，适合 CI：

```powershell
$Editor  = '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = '<Project>\<Project>.uproject'

& $Editor $Project -run=UBotPackage -List
& $Editor $Project -run=UBotPackage -Validate
& $Editor $Project -run=UBotPackage -Install=UBotSensor
& $Editor $Project -run=UBotPackage -Enable=UBotSensor
& $Editor $Project -run=UBotPackage -Disable=UBotSensor -Force
& $Editor $Project -run=UBotPackage -Update=UBotSensor -Timeout=600
```

| 参数 | 说明 |
| --- | --- |
| `-List` | 以表格打印已安装和索引中的包（Name、Version、Layer、State、Problems），并列出各包的问题 |
| `-Validate` | 校验已安装的包；有任何问题（包括索引错误）则失败 |
| `-Install=<Name>` | 与窗口中的 Install 相同，等待完成 |
| `-Update=<Name>` | 与窗口中的 Update 相同，等待完成 |
| `-Enable=<Name>` | 启用该包及其必需依赖 |
| `-Disable=<Name> [-Force]` | 禁用该包；有已启用的依赖者时，需要 `-Force` 才会连同它们一起禁用 |
| `-Timeout=<Seconds>` | 等待 `-Install` / `-Update` 的最长时间，默认 1800，`0` 表示一直等待。超时会取消 git |

一次可以组合多个参数，执行顺序固定为 Install、Update、Enable、Disable，最后是 List 和 Validate。不带任何动作参数时打印用法并列出包。退出码：全部成功为 `0`；任一操作失败、`-Validate` 发现问题或参数有误（动作或 `-Timeout` 缺少 `=<value>`、超时值无法解析）为 `1`。Enable / Disable / Install 改的是 `.uproject`，之后需要重新启动编辑器才会载入新状态。

### 扩展点

扩展注册表（`UBotExtensionRegistry.h`）让一个包不依赖另一个包的模块就能扩展它：声明方定义一个继承 `IUBotExtension` 的接口并声明扩展点，扩展方实现该接口并注册。

```cpp
#include "UBotExtensionRegistry.h"

// 声明方（包 MyExporter）：定义扩展必须实现的接口，并在文档中写明。
class IMyExportFormat : public IUBotExtension
{
public:
    virtual FString GetFileExtension() const = 0;
};

// MyExporter 的 StartupModule：
FUBotExtensionRegistry::Get().RegisterExtensionPoint(
    TEXT("MyExporter.Format"), TEXT("MyExporter"),
    NSLOCTEXT("MyExporter", "FormatPoint", "File formats the exporter can write."));

// 声明方使用扩展：
for (const TSharedRef<IMyExportFormat>& Format :
    FUBotExtensionRegistry::Get().GetExtensionsAs<IMyExportFormat>(TEXT("MyExporter.Format")))
{
    // ...
}

// 扩展方（包 MyCsvFormat）：
class FCsvFormat : public IMyExportFormat
{
public:
    virtual FName GetExtensionName() const override { return TEXT("Csv"); }
    virtual FString GetFileExtension() const override { return TEXT("csv"); }
};

// MyCsvFormat 的 StartupModule / ShutdownModule：
FUBotExtensionRegistry::Get().RegisterExtension(
    TEXT("MyExporter.Format"), TEXT("MyCsvFormat"), MakeShared<FCsvFormat>());
// ...
FUBotExtensionRegistry::Get().UnregisterAllFromPackage(TEXT("MyCsvFormat"));
```

要点：

- `OwnerPackage` 用插件名，包管理器窗口按它显示该包拥有的扩展点和扩展。
- 每个包必须在 `ShutdownModule` 中调用 `UnregisterAllFromPackage`，避免扩展比实现它的代码活得更久。
- 一个扩展点只能由一个包声明（同一个包重复声明会更新描述）；同一扩展点下扩展名必须唯一，重复注册返回 `false`。
- 扩展可以先于扩展点注册（模块加载顺序），会被保留，扩展点声明后才通过 `GetExtensions` 返回；扩展点声明之前该函数返回空。
- 按注册顺序返回；`GetExtensionsAs<T>` 用 `StaticCastSharedRef`，`T` 由声明方文档约定，请保证类型正确。
- 注册表线程安全，`OnExtensionsChanged` 在内部锁之外广播。
- 其他查询：`GetExtensionPoints`、`GetAllExtensions`、`IsExtensionPointDeclared`、`UnregisterExtension`。

### 蓝图与 C++ 查询

`UUBotPackageLibrary`（类别 `uBot|Packages`）：`Get Installed uBot Packages`、`Find uBot Package`、`Is uBot Package Enabled`、`Get uBot Package Version`（未安装则为空字符串）、`Is uBot Package Version Satisfied`（包已安装且版本满足约束，例如 `^0.1.0`）、`Get uBot Extension Point Names`、`Get uBot Extension Names`。

C++ 使用 `FUBotPackageRegistry::Get()`：`GetInstalledPackages`、`FindPackage`、`IsPackageEnabled`、`Refresh`、`ValidateAndLog` 和 `OnPackagesChanged`；纯函数 `ParseDescriptor`、`ValidatePackageSet`、`ResolveDependencyOrder`、`FindDependents` 可在合成的包集合上直接使用。

## 测试

测试名都以 `UBotCore.` 开头，分类为 `Clock`、`Conventions`、`Extensions`、`Identity`、`Interfaces`、`Medium`、`PackageManager`、`Packages`、`Scheduler`。`PackageManager` 测试在 `UBotCoreEditor` 模块里。测试需要在启用了 UBotCore 的工程中，用带编辑器上下文的 `UnrealEditor` 运行（`Packages.RegistryFindsCore` 等会读取已安装的插件）。

编辑器中：打开 Session Frontend（`Tools` 菜单）的 Automation 标签页，过滤 `UBotCore`。

命令行：

```powershell
& '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<Project>\<Project>.uproject' -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests UBotCore; Quit' '-TestExit=Automation Test Queue Empty'
```

把 `UBotCore` 换成更具体的前缀（如 `UBotCore.Packages`）可以只跑一类。

## 版本与许可

当前版本 **0.1.0**（Beta，`IsBetaVersion`）。作者 AstreoX，仓库 <https://github.com/AstreoX/uBotCore>。

仓库目前没有包含 LICENSE 文件，因此尚未给出明确的使用、修改和再分发许可；在补充许可证之前，请不要假定可以再分发。
