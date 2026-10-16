
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

## 坐标约定（UE 与右手系 SI）

UE 是左手系（X 前、Y 右、Z 上，单位厘米）。外部世界通常是右手系、X 前、Y 左、Z 上、SI 单位（FLU）：ROS 遵循它（REP-103），MuJoCo 等右手 Z-up 引擎在模型的 X 轴朝前时也一样。两者之间只差镜像 `M = diag(1, -1, 1)` 和厘米到米的换算，所有映射都是自逆的，`ToUE` 与 `FromUE` 公式相同（仅单位换算不同）。

`UBotRightHandedConventions.h` 的 `UBot::RightHanded` 命名空间是纯数学函数，不含任何协议或引擎的类型；同一组函数也以蓝图节点的形式提供（`UUBotConventionsLibrary`，类别 `uBot|Conventions`）。节点的关键字里保留了 `ROS REP103`，搜索 “ROS” 仍能找到它们。

| 量 | UE → 右手系 | `UBot::RightHanded` 函数（反向为 `…ToUE`） | 蓝图节点（`UE To Right-Handed …` / `Right-Handed To UE …`） |
| --- | --- | --- | --- |
| 位置 | `(x, -y, z) / 100`（cm → m） | `PositionFromUE` | `… Position` |
| 线性量（线速度、加速度、已是米的点） | `(x, -y, z)` | `VectorFromUE` | `… Vector` |
| 轴向量（角速度、力矩） | `(-x, y, -z)` | `AngularVelocityFromUE` | `… Angular Velocity` |
| 旋转四元数 | `(x, y, z, w)` → 归一化的 `(-x, y, -z, w)` | `RotationFromUE` | `… Rotation` |
| 刚体变换 | 旋转镜像，平移 cm → m，缩放丢弃（输出缩放为 1） | `TransformFromUE` | `… Transform` |
| 航向、扫描角、偏航角速度 | `右手系 = -UE` | `YawFromUE` | `… Yaw` |

两个方向共 12 个蓝图节点，函数名为 `PositionUEToRightHanded` / `PositionRightHandedToUE`，其余类推（`Vector`、`AngularVelocity`、`Rotation`、`Transform`、`Yaw`）。

旧入口仍然可用，但已标记弃用，结果与新入口逐位相同：

- 蓝图的 12 个 `UE To ROS …` / `ROS To UE …` 节点（如 `PositionUEToROS`）和 C++ `UBot::Ros` 里对应的 12 个函数。蓝图里使用它们会看到弃用提示，指向对应的 `UE To Right-Handed …` 节点；C++ 里它们直接转发到 `UBot::RightHanded`。
- ROS 专属的内容属于 [uBot ROS](https://github.com/AstreoX/uBotROS) 插件，已移到那里：相机光学系旋转 `CameraLinkToOpticalRotation`、光学系后缀 `OpticalFrameSuffix`（`_optical`）、秒与 `builtin_interfaces/Time` 互转的 `SecondsToRosTime` / `RosTimeToSeconds`、ROS 名字合法化 `SanitizeRosName`。C++ 在 `UBot::ROSConventions`（`UBotROSConventionHelpers.h`），蓝图节点在类别 `uBot|ROS|Conventions`，名字和显示名不变。Core 里同名的 5 个蓝图节点和 `UBot::Ros` 函数保持原有行为，但已弃用；其中 `UBot::Ros::SanitizeRosName` 转发到 `UBot::Identity::SanitizeNameToken`（身份组件命名空间回退时用的同一规则，见 [Identity 组件](https://astreox.github.io/uBotDocs/core/identity/)）。

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
- `ConsumeDueSteps(NowSeconds, MaxSteps, OutFirstDueSeconds = nullptr)`：给固定步长的仿真用。数出从下一次到期时间起、不晚于 `NowSeconds` 的到期点，最多 `MaxSteps` 个，返回这个个数；下一次到期时间随后推进到所有不晚于 `NowSeconds` 的网格点之后，超出 `MaxSteps` 的（较晚的）到期点被跳过并计入丢弃数。`OutFirstDueSeconds`（可选）得到第一个被计入的到期时间，第 k 个是 `OutFirstDueSeconds + k * GetIntervalSeconds()`。`MaxSteps < 1`、`NowSeconds` 不是有限数或早于下一次到期时间、调度器未初始化时返回 0（此时不写 `OutFirstDueSeconds`）。它和 `ConsumeDue` 共用同一份进度：`MaxSteps` 为 1 时与 `ConsumeDue` 完全等价。
- 到期时间按网格 `FirstDue + k / RateHz` 由序号计算，不累加，长时间运行不会漂移。
- 其他访问器：`GetRateHz`、`GetIntervalSeconds`、`GetNextDueSeconds`、`GetDroppedSampleCount`、`IsInitialized`。

按固定步长推进的求解器每帧补步，物理时间永远不超过仿真时间，一帧里最多补 `MaxSteps` 步：

```cpp
FUBotSampleScheduler Steps;

// BeginPlay：固定步长 1/60 s，第一步在 Now + 1/60 s 到期
Steps.Reset(60.0, Now + 1.0 / 60.0);

// 每个 tick：
double FirstDue = 0.0;
const int32 Count = Steps.ConsumeDueSteps(UBot::Clock::GetSimTimeSeconds(this), 1000, &FirstDue);
for (int32 Index = 0; Index < Count; ++Index)
{
    Solver.Step(FirstDue + Index * Steps.GetIntervalSeconds()); // 这一步结束时的仿真时间
}
```

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

## 刚体状态源（实验性）

`UBotBodyState.h` 定义了“谁在驱动一个刚体、它的真实状态是什么”的中立接口，供物理后端的绑定、飞行器自己的积分器等实现。传感器和接入点可以读取状态，而不必对位姿做差分。接口和结构可能随小版本改变。

- `FUBotBodyState`（普通 C++ 结构体，不是反射类型）：刚体原点处的状态，UE 世界坐标轴、SI 单位。字段：`PositionMeters`、`Orientation`（机体到世界）、`LinearVelocityMetersPerSecond`、`AngularVelocityRadiansPerSecond`（UE 旋转方向，+Z 为向右偏航）、`LinearAccelerationMetersPerSecondSquared`（原点的运动学加速度，不含重力，`bHasLinearAcceleration` 为真时有效）、`AngularAccelerationRadiansPerSecondSquared`（`bHasAngularAcceleration` 为真时有效）、`TimestampSeconds`（状态对应的 uBot 仿真时间，不晚于当前仿真时间）、`Source`（谁模拟的，例如后端名）。
- `IUBotBodyStateSource`（`UINTERFACE`，只能在 C++ 里实现）：`ProvidesBodyState(Body)` 为真表示这个来源在驱动（写入变换）该刚体并能报告状态；`GetBodyState(Body, OutState)` 取最新状态，刚体不归它或还没有状态时返回 `false`。提供一个刚体同时也是认领它：别人不应再去移动它。
- `UBot::BodyState::FindSource(Body)`：先问刚体所属的 Actor（如果它实现了接口），再按 `GetComponents` 的顺序问它的组件，第一个 `ProvidesBodyState(Body)` 为真的对象胜出；没有，或 `Body` 为空，返回 `nullptr`。
- `UBot::BodyState::FindSourceInParents(Component, OutBody)`：从 `Component` 起沿附着父级向上，返回第一个找到的来源，并把它提供的刚体写入 `OutBody`；没有则返回 `nullptr`，`OutBody` 也为 `nullptr`。
- `UBot::BodyState::GetNearestBodyState(Component, OutState, OutBody = nullptr)`：`Component` 自己或其附着父级中最近的被提供刚体的状态。`OutBody`（可选）得到这个刚体（它还没有状态时也会得到），只有返回 `true` 才写 `OutState`。

```cpp
FUBotBodyState State;
const USceneComponent* Body = nullptr;
if (UBot::BodyState::GetNearestBodyState(SensorComponent, State, &Body))
{
    // State 是 Body 的真实状态；Body 可能是 SensorComponent 的某一级附着父级
}
```
