
# Package 管理参考

下面的内容原样移自重写前的 README，文档站（https://astreox.github.io/uBotDocs/）上对应的页面没有覆盖其中的细节。

## `.uplugin` 中的 `"UBot"` 块

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
        "Tags": [ "sensor" ],
        "EnginePlugins": [ "ChaosVehiclesPlugin" ]
    }
}
```

| 字段 | 说明 |
| --- | --- |
| `VersionName`（原生） | Package 版本，应为语义化版本；缺失或无法解析会记为描述符问题 |
| `FriendlyName`、`Description`、`DocsURL`（原生） | 显示信息；`FriendlyName` 缺省为插件名 |
| `Layer` | `Foundation`、`Capability`、`Adapter`、`Content`（不区分大小写）；缺失或无法识别（包括已不再是层的 `Composition`）记为描述符问题 |
| `Repository` | 安装与更新使用的 git 仓库 URL |
| `Requires` | 依赖列表，每项含 `Name`、`Version`（版本约束，缺省为任意版本）、`Optional`（默认 `false`） |
| `Provides` | 本 Package 提供的能力键，如 `Sensor.IMU` |
| `Tags` | 标签 |
| `EnginePlugins`（可选） | 必须启用的引擎插件（如 `ChaosVehiclesPlugin`）。旧键名 `ExternalRequires` 仍然可用，两者同时存在时合并 |
| `DocsUrl`（可选） | 覆盖原生 `DocsURL` |

只有存在 `"UBot"` 对象的插件才会被当作 uBot Package。可选依赖（`Optional: true`）只在对方已安装但版本不满足时才算问题，且不构成依赖边。

`"UBot"` 块要和 [`Index/index.json`](../Index/index.json) 里对应版本的条目保持一致，写法见 [`Index/README.md`](../Index/README.md)。

## 版本约束语法

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

## 校验

`FUBotPackageRegistry::ValidatePackageSet` 为每个 Package 计算问题列表（`Problems` 是文字，`ProblemDetails` 是同序的结构化形式：类型、依赖名、约束）：

- 必需的 Package 不存在或未安装
- 已安装的依赖不满足版本约束
- 已启用的 Package 依赖了未启用的 Package
- 版本约束无法解析
- 依赖循环

默认在所有模块加载完成后做一次校验（`bValidatePackagesOnStartup`），每个问题以 `LogUBot` 警告输出，并以 `uBot packages: N installed, M enabled, K problem(s).` 收尾；也会提示注册在从未声明的扩展点上的扩展。已启用的 Package 的问题同样显示在 uBot 面板的 Problems 分区中，以及 commandlet 的 `-Validate` 中。

## Package 索引

Package 索引（格式 2）列出已知的 Package，包括尚未安装和还在规划中的 Package，以及被 uBot Package 取代的旧插件。主索引在本仓库的 [`Index/index.json`](../Index/index.json)，格式、字段、索引源与回退、添加 Package 和新版本的步骤见 [`Index/README.md`](../Index/README.md)。格式 1（旧的 `Resources/PackageIndex.json`）不再支持，读到时会报错并说明需要转换。

UE 中的加载顺序（`FUBotPackageIndex::LoadConfiguredIndex`）：

1. `<Project>/Saved/uBot/index.json`：uBot Manager 每次刷新后写入的合并索引，存在时使用；
2. 否则使用本插件的 `Index/index.json`；
3. 然后按设置顺序加载 `AdditionalPackageIndexFiles`，同名条目由后加载的覆盖，位置保持不变。

```ini
[/Script/UBotCore.UBotCoreSettings]
+AdditionalPackageIndexFiles=Config/uBotPackages.json
```

每个索引条目变成一个 `FUBotPackageInfo`：`Version` 为最新的非规划版本（按 SemVer 排序），`AvailableVersions` 为所有非规划版本（新的在前），只有规划版本的 Package `bPlanned` 为 `true`（`Version` 取最新的规划版本）；`Requires`、`Provides`、`ExternalRequires`（即 `enginePlugins`）取自同一个版本；`Description` 按编辑器语言选择（中文界面用 `zh-CN`，其余及缺失时用 `en`）。读取失败的文件会在 uBot 面板和日志中报告，不会影响其他文件。

索引与已安装的 Package 合并后得到 Package 视图：已安装的 Package 以其自身 `.uplugin` 的数据为准，带上索引中的 `AvailableVersions` 与 `bPlanned`，只在 `Repository` / `DocsUrl` 为空时用索引补齐；只出现在索引里的 Package 为 Not installed。

## uBot 面板

从 **Window > uBot** 打开（可停靠的 nomad 标签页 `UBotPanel`），或点击状态栏右侧带 uBot 标志的 `uBot: 3 packages · 2 problems`（中文界面是 `uBot：3 个 Package · 2 个问题`）。也可以用控制台命令 `uBot.OpenPanel`，在脚本里是 `UnrealEditor.exe <Project>.uproject -ExecCmds="uBot.OpenPanel"`。

- **Open uBot Manager**：启动 uBot Manager 并传入 `--project "<当前 .uproject 的绝对路径>"`。依次查找 `Editor Preferences > Plugins > uBot` 的 `Manager Executable`（每个用户、每个工程单独保存），`HKCU\Software\AstreoX\uBot Manager` 的 `Executable` 值（uBot Manager 启动时写入），`%LOCALAPPDATA%\uBot Manager\ubot-manager.exe`；都找不到时弹出带下载链接的通知。旁边的刷新按钮重新扫描插件并重新读取索引。
- **Loaded Packages**：已启用的 uBot Package 和版本；索引里有更新的版本时显示 `0.1.0 · 0.1.1 available`（中文界面是 `0.1.0 · 可更新到 0.1.1`）。
- **Extension Points**：已声明的扩展点和实现数量。
- **Runtime**：各 Package 发布的运行时状态（如 ROS 桥接），PIE 运行时的仿真时间（每 0.25 s 刷新），固定时间步长，以及上次运行的类型和时间。
- **Problems**：已启用的 Package 的依赖问题、描述符问题、与 uBot Package 同时启用的旧插件（索引中的 `replacements`）、运行时问题和索引读取错误。

面板只显示，不做安装、更新、启用或禁用。它随 Package 注册表、扩展注册表和运行时状态的变化自动刷新；文字跟随编辑器语言（简体中文由模块启动时注册）。

## 运行时状态

`FUBotRuntimeStatus`（`UBotRuntimeStatus.h`，游戏线程）收集各 Package 运行时发布的状态项和问题：

```cpp
FUBotRuntimeItem Item;
Item.Id = TEXT("ros.bridge");
Item.Label = LOCTEXT("BridgeLabel", "ROS bridge");
Item.Value = TEXT("connected");                // 写入 status.json
Item.ValueText = LOCTEXT("Connected", "Connected"); // 面板显示
Item.Detail = TEXT("127.0.0.1:8268");
Item.Severity = EUBotRuntimeSeverity::Ok;      // Ok、Info、Warning、Error
FUBotRuntimeStatus::Get().SetItem(Item);       // RemoveItem 移除；ReportProblem / ClearProblem 管理问题
```

状态项和问题由发布它们的 Package 负责移除，不会在会话开始或结束时被清空。编辑器中，一次 PIE 就是一个会话；打包后的游戏中，会话从第一个游戏世界 BeginPlay 开始，到该世界销毁为止。会话结束时（以及会话进行中模块关闭时）写入 `<Project>/Saved/uBot/status.json`：先写临时文件再移动过去，每次覆盖，所以只保留最近一次运行。

```json
{
    "formatVersion": 1,
    "writtenAt": "2026-10-10T08:00:00Z",
    "session": { "kind": "PIE", "startedAt": "2026-10-10T07:50:00Z", "endedAt": "2026-10-10T08:00:00Z" },
    "engineVersion": "5.5.4",
    "packages": [ { "name": "UBotCore", "version": "0.1.0", "enabled": true } ],
    "extensionPoints": [ { "id": "UBotROS.SensorPublisher", "implementations": 7 } ],
    "runtime": [
        { "id": "ros.bridge", "label": "ROS bridge", "value": "disconnected", "detail": "127.0.0.1:8268", "severity": "warning" }
    ],
    "problems": [
        { "id": "ros.bridge", "package": "UBotROS", "severity": "info", "code": "rosBridgeUnreachable",
          "params": { "address": "127.0.0.1:8268" }, "message": "Could not connect to ubot_ros_bridge at 127.0.0.1:8268." }
    ]
}
```

| 字段 | 说明 |
| --- | --- |
| `session` | `kind` 目前是 `PIE` 或 `Game`；时间为 UTC |
| `packages` | 已安装的 uBot Package，含版本和是否启用 |
| `extensionPoints` | 已声明的扩展点和当时的实现数量 |
| `runtime` | 状态项：`value` 是机器可读的状态，`severity` 为 `ok`、`info`、`warning` 或 `error`；`label` 是英文原文 |
| `problems` | 问题：`code` 加 `params` 是机器可读的形式，`message` 是英文原文 |

uBot Manager 目前读取 `session` 和 `problems`：在 Problems 视图的“上次运行”范围里显示这些问题和运行时间，不依赖界面语言。`code` 是 uBot Manager 认识的代码（如 `rosBridgeUnreachable`）时显示它自己的翻译，并提供对应的快速修复（`rosBridgeUnreachable` 是“打开 ROS 设置”）；否则显示 `message`。

## 扩展点

扩展注册表（`UBotExtensionRegistry.h`）让一个 Package 不依赖另一个 Package 的模块就能扩展它：声明方定义一个继承 `IUBotExtension` 的接口并声明扩展点，扩展方实现该接口并注册。

```cpp
#include "UBotExtensionRegistry.h"

// 声明方（Package MyExporter）：定义扩展必须实现的接口，并在文档中写明。
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

// 扩展方（Package MyCsvFormat）：
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

- `OwnerPackage` 用插件名。
- 每个 Package 必须在 `ShutdownModule` 中调用 `UnregisterAllFromPackage`，避免扩展比实现它的代码活得更久。
- 一个扩展点只能由一个 Package 声明（同一个 Package 重复声明会更新描述）；同一扩展点下扩展名必须唯一，重复注册返回 `false`。
- 扩展可以先于扩展点注册（模块加载顺序），会被保留，扩展点声明后才通过 `GetExtensions` 返回；扩展点声明之前该函数返回空。
- 按注册顺序返回；`GetExtensionsAs<T>` 用 `StaticCastSharedRef`，`T` 由声明方文档约定，请保证类型正确。
- 注册表线程安全。扩展点或扩展变化后，变化通知在内部数据锁之外、在改动注册表的那个线程上广播，所以处理函数里可以回调注册表；它也因此可能不在游戏线程上运行，处理函数里不要等待只有改动注册表才能完成的工作。
- 监听变化用 `AddExtensionsChangedHandler(Delegate)`（返回句柄），不再监听时用 `RemoveExtensionsChangedHandler(Handle)`。这两个函数与广播使用同一把锁，所以别的线程正在注册、注销扩展时也可以安全调用；`RemoveExtensionsChangedHandler` 返回后，处理函数不会还在别的线程里运行，也不会再被调用。不要在注册表的处理函数里调用这两个函数。公开成员 `OnExtensionsChanged` 为兼容保留，但直接对它绑定、解除绑定不会与别的线程的广播同步，请改用上面两个函数。

  ```cpp
  Handle = FUBotExtensionRegistry::Get().AddExtensionsChangedHandler(
      FUBotExtensionRegistry::FOnExtensionsChanged::FDelegate::CreateUObject(this, &UMyObject::HandleExtensionsChanged));
  // ...
  FUBotExtensionRegistry::Get().RemoveExtensionsChangedHandler(Handle);
  ```
- 其他查询：`GetExtensionPoints`、`GetAllExtensions`、`IsExtensionPointDeclared`、`UnregisterExtension`。

## 蓝图与 C++ 查询

`UUBotPackageLibrary`（类别 `uBot|Packages`）：`Get Installed uBot Packages`、`Find uBot Package`、`Is uBot Package Enabled`、`Get uBot Package Version`（未安装则为空字符串）、`Is uBot Package Version Satisfied`（Package 已安装且版本满足约束，例如 `^0.1.0`）、`Get uBot Extension Point Names`、`Get uBot Extension Names`。

C++ 使用 `FUBotPackageRegistry::Get()`：`GetInstalledPackages`、`FindPackage`、`IsPackageEnabled`、`Refresh`、`ValidateAndLog` 和 `OnPackagesChanged`；纯函数 `ParseDescriptor`、`ValidatePackageSet`、`ResolveDependencyOrder`、`FindDependents` 可在合成的 Package 集合上直接使用。
