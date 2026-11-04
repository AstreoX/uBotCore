
# 开发

下面的内容原样移自重写前的 README，文档站（https://astreox.github.io/uBotDocs/）上对应的页面没有覆盖其中的细节。

## 模块

| 模块 | 类型 | 加载阶段 | 说明 |
| --- | --- | --- | --- |
| `UBotCore` | Runtime | PreDefault | 运行时 API |
| `UBotCoreEditor` | Editor | Default | uBot 面板、PIE 会话记录、`UBotPackage` 与 `UBotDocExport` commandlet |

## 测试

测试名都以 `UBotCore.` 开头，分类为 `BodyState`、`Clock`、`Conventions`、`Editor.Conventions`、`Editor.DocExport`、`Extensions`、`Identity`、`Interfaces`、`Medium`、`PackageManager`、`Packages`、`Panel`、`RuntimeStatus`、`Scheduler`。`Editor.Conventions`、`Editor.DocExport`、`PackageManager` 与 `Panel` 测试在 `UBotCoreEditor` 模块里。测试需要在启用了 UBotCore 的工程中，用带编辑器上下文的 `UnrealEditor` 运行（`Packages.RegistryFindsCore` 等会读取已安装的插件）。

几类测试的范围：

- `Conventions`：`UBot::RightHanded` 的轴映射、往返、旋转与向量镜像一致、角速度与四元数差分一致、变换与点一致、偏航角（`AxisMapping`、`RoundTrips`、`RotationMatchesVectorMirror`、`AngularVelocityMatchesQuat`、`TransformMatchesPoints`、`YawMatchesRotation`），12 个正式蓝图节点（`BlueprintNodes`），以及 17 个旧 ROS 换算函数不再反射可见的检查（`RemovedRosApi`）。光学系、ROS 时间和 ROS 名称行为由 `UBotROS.Conventions.*` 覆盖。
- `Editor.Conventions.BlueprintMigration`：旧数学调用节点与改名引脚的重定向、重构、编译及执行比较；启用 uBotROS 时额外验证跨类迁移，不增加 Core 的模块依赖。
- `Identity.SanitizeNameToken`：17 个固定用例和 500 个随机字符串。
- `Scheduler.ConsumeDueSteps`：网格上的精确个数与首个到期时间、`MaxSteps` 上限与丢弃数、非法输入返回 0、与 `ConsumeDue` 共用进度。
- `BodyState`：默认值，以及 `FindSource`、`FindSourceInParents`、`GetNearestBodyState` 的查找顺序与空输入。
- `Extensions.ChangeHandlers`：`AddExtensionsChangedHandler` / `RemoveExtensionsChangedHandler` 的基本行为；游戏线程反复添加、移除处理函数的同时，工作线程循环注册、注销扩展：一直绑定的处理函数恰好收到每一次变化，移除之后没有任何处理函数再被调用或仍在运行。
- `Packages.BuiltInIndex`：已安装的 uBot Package 与索引里同版本的条目（计划中的版本也算）在依赖、能力键、标签、层级和引擎插件上一致；索引的“已发布状态”（有发布版本的 Package 各取最新发布版本）和“公告状态”（每个 Package 取最新版本，含计划版本）各自没有依赖问题。
- `Editor.DocExport.FunctionNodes`：正式坐标换算节点及其引脚，以及独立测试夹具的 `deprecated` 标记。

编辑器中：打开 Session Frontend（`Tools` 菜单）的 Automation 标签页，过滤 `UBotCore`。

命令行：

```powershell
& '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<Project>\<Project>.uproject' -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests UBotCore; Quit' '-TestExit=Automation Test Queue Empty'
```

把 `UBotCore` 换成更具体的前缀（如 `UBotCore.Packages`）可以只跑一类。
