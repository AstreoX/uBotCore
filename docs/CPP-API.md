# C++ API 参考

下面的内容原样移自重写前的 README，文档站（https://astreox.github.io/uBotDocs/）上对应的页面没有覆盖其中的细节。

## 仿真时钟与单步

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

## UE 与 ROS 坐标约定

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

## 采样调度器

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

## 接口

六个接口都是 `UINTERFACE(BlueprintType)`，函数为 `BlueprintNativeEvent`，C++ 组件和蓝图 Actor 都可以实现。调用方先 `Object->Implements<UUBotX>()`，再用 `IUBotX::Execute_Fn(Object, ...)`，这样原生与蓝图实现都能走到。

| 接口 | 头文件 | 函数 |
| --- | --- | --- |
| `IUBotSensor` | `UBotSensorInterface.h` | `GetSensorId`、`GetSensorFrameId`、`GetSensorType`（短而稳定的类型键，如 `Pose`、`IMU`、`Lidar2D`）、`IsSensorRunning`、`GetLatestSensorHeader`（`FUBotFrameHeader`）、`GetSensorFrameComponent`（作为 TF frame 的场景组件，可为空） |
| `IUBotActuator` | `UBotActuatorInterface.h` | `GetActuatorId`、`GetActuatorType`、`IsActuatorReady` |
| `IUBotVelocityCommandable` | `UBotActuatorInterface.h` | `ApplyVelocityCommand(const FUBotVelocityCommand&)`（被拒绝时返回 `false`）、`StopMotion`。独立的能力接口，不要求同时实现 `IUBotActuator` |
| `IUBotAttitudeCommandable` | `UBotActuatorInterface.h` | `ApplyAttitudeCommand(const FUBotAttitudeCommand&)`、`ApplyBodyRateCommand(const FUBotBodyRateCommand&)`（被拒绝时返回 `false`）、`ReleaseAttitudeCommand`（发送方停了，由飞行器自己兜底，比如悬停）。给飞行器用 |
| `IUBotMotorCommandable` | `UBotActuatorInterface.h` | `ApplyMotorCommand(const FUBotMotorCommand&)`（值的个数与电机数不符等情况下返回 `false`）、`GetMotorCount`、`ReleaseMotorCommand` |
| `IUBotPerceptionMedium` | `UBotPerceptionMedium.h` | `GetMediumSnapshot(FUBotMediumSnapshot&)` |

查找辅助函数：

- `UBot::Sensors::FindSensors(Actor, OutSensors)`：返回 Actor 自身（若实现接口）加上所有实现 `IUBotSensor` 的组件，按组件顺序。
- `UBot::Actuation::FindVelocityCommandable(Actor)`、`FindAttitudeCommandable(Actor)`、`FindMotorCommandable(Actor)`：先 Actor 本身，再其组件，没有则 `nullptr`。
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

`FUBotVelocityCommand` 在机体系下、使用 UE 坐标轴（X 前、Y 右、Z 上）：线速度单位 m/s，角速度单位 rad/s 且采用 UE 旋转方向（+Z 为向右偏航）。`Source` 是自由格式的来源标签，如 `ROS`、`MCP`、`Blueprint`。其他坐标系的生产者（例如 ROS 适配器）负责先转换再填充。机体会倾斜的接收者（多旋翼）按航向坐标系理解它：X、Y 沿当前航向，Z 朝上。

飞行器的姿态与电机指令也用 UE 坐标轴和 SI 单位：

| 结构体 | 字段 |
| --- | --- |
| `FUBotAttitudeCommand` | `Orientation`（机体到世界的四元数）；`bUseYawRate` 和 `YawRateRadiansPerSecond`（按角速度转航向，不跟随 `Orientation` 的偏航）；`bUseThrust` 和 `NormalizedThrust`（总推力占最大值的比例，0..1）；不给推力时由接收者按 `VerticalVelocityMetersPerSecond` 控制升降（0 为保持高度） |
| `FUBotBodyRateCommand` | `BodyRateRadiansPerSecond`（机体系角速度，UE 旋转方向）；推力字段同上 |
| `FUBotMotorCommand` | `Type`（`Normalized`：0..1；`AngularVelocity`：rad/s）；`Values`（每个电机一个值，按接收者的电机顺序） |

三者都带 `TimestampSeconds`（仿真时间）和 `Source`。

感知介质（烟、雾等）由球形单元组成：

- `FUBotMediumCell`：`WorldPositionCentimeters`、`RadiusCentimeters`（小于 1 cm 按 1 cm）、`Density`（0 到 1）、`EdgeFactor`（0 到 1，向介质边缘衰减密度）。
- `FUBotMediumSnapshot`：`MediumType`（默认 `Smoke`）、`bActive`、`Tint`、`Cells`，以及非 UPROPERTY 的 `BoundsCentimeters` 和 `Source`。修改 `Cells` 后需要再次调用 `FinalizeSnapshot`，否则包围盒过期会让相交测试漏掉单元。
- `UBot::Medium`：`EffectiveDensity(Cell)`（`Density * EdgeFactor`，限制在 0 到 1）、`FinalizeSnapshot`、`CollectSnapshots(World, Out)`（收集世界中所有实现了 `IUBotPerceptionMedium` 且处于激活并非空的快照）、`FindFirstIntersection(Start, End, Snapshots, DensityThreshold, OutDistance, OutDensity)`（线段上第一个进入有效密度不低于阈值的单元的位置，距离从起点量起，单位厘米；起点已在单元内时为 0）。
