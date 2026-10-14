# Commandlet 参考

下面的内容原样移自重写前的 README，文档站（https://astreox.github.io/uBotDocs/）上对应的页面没有覆盖其中的细节。

## 命令行（commandlet）

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
| `-List` | 以表格打印已安装和索引中的 Package（Name、Version、Layer、State、Problems），并列出各 Package 的问题。State 为 Planned、Not installed、Enabled、Disabled，或改过项目文件但尚未重启时的 Enabled after restart / Disabled after restart |
| `-Validate` | 校验已安装的 Package；有任何问题（包括索引错误）则失败 |
| `-Install=<Name>[@<Version>]` | 按依赖顺序克隆缺失的必需依赖和该 Package 本身，注册后启用该 Package，等待完成。`@<Version>` 选择 Package 索引中该版本条目（取其 `ref`），省略时安装最新的可安装版本；必需依赖始终取最新版本。`<Name>` 已安装时，`@<Version>` 必须等于已安装的版本（不克隆，直接成功），否则失败 |
| `-Update=<Name>` | 在 Package 目录执行 `git -C <BaseDir> pull --ff-only`，等待完成 |
| `-Enable=<Name>` | 启用该 Package 及其必需依赖 |
| `-Disable=<Name> [-Force]` | 禁用该 Package；有已启用的依赖者时，需要 `-Force` 才会连同它们一起禁用 |
| `-Timeout=<Seconds>` | 等待 `-Install` / `-Update` 的最长时间，默认 1800，`0` 表示一直等待。超时会取消 git |

一次可以组合多个参数，执行顺序固定为 Install、Update、Enable、Disable，最后是 List 和 Validate。不带任何动作参数时打印用法并列出 Package。退出码：全部成功为 `0`；任一操作失败、`-Validate` 发现问题或参数有误（动作或 `-Timeout` 缺少 `=<value>`、超时值无法解析）为 `1`。Enable / Disable / Install 改的是 `.uproject`，之后需要重新启动编辑器才会载入新状态。

`-Update` 只是 `git pull --ff-only`，适合跟随分支；按标签克隆出来的目录是游离 HEAD，pull 会失败。要换到另一个版本，请用 uBot Manager，它会检出对应的标签。

`-Install` 的细节：仓库地址只取自 Package 索引；规划中的 Package 不能安装；克隆命令为 `git -c advice.detachedHead=false clone [--branch <ref>] -- <repository> <工程目录>/<PackageInstallDirectory>/<文件夹>`。`<ref>` 是所选索引版本的 `ref`：分支或标签用 `--branch` 直接克隆；提交 ID（7 到 40 位十六进制）不带 `--branch`，克隆后再执行 `git -c advice.detachedHead=false -C <目录> checkout --detach <提交> --`。`<文件夹>` 是索引条目的 `folder`，未设置时取仓库 URL 最后一段去掉 `.git`（二者都必须是单个安全的文件夹名）。目标文件夹已存在时拒绝，仓库根目录必须有 `<Package 名>.uplugin`；被取消的克隆留下的半成品目录会被删除。`EnginePlugins` 不会被安装或启用，只在缺失或未启用时给出提示。

git 的运行方式：不经过 shell，参数逐个加引号；只允许 `https`、`http`、`ssh`、`file` 协议、scp 形式（`user@host:path`）和绝对本地路径；`git://`（无认证、无加密）、相对路径和 `host:path` 形式会被拒绝，以 `-` 开头的地址和 `<transport>::` 形式的 remote helper 同样被拒绝；输出和日志中的 URL 凭据会被替换为 `***`；设置了 `GIT_TERMINAL_PROMPT=0`，所以私有仓库需要事先配置好 SSH key 或 credential helper，git 不会弹出交互式提示。

### 蓝图 API 导出（UBotDocExport）

`UBotDocExport` commandlet 把一个插件的蓝图 API（或一份引擎节点清单）写成 uBot 文档站使用的 `api.json`（格式 1）。节点通过蓝图编辑器自己的节点生成器放进一个临时 Actor 蓝图再读回，所以节点标题、引脚名、引脚顺序和隐藏引脚都与蓝图编辑器画出来的一致。

```powershell
# 中文导出，再导出英文并合并中文字段（titleZh、subtitleZh、nameZh、displayNameZh，只在与英文不同时写入）
& $Editor $Project -run=UBotDocExport -Plugin=UBotSensor -Out=Saved/Docs/UBotSensor.zh.json -culture=zh-Hans -NullRHI
& $Editor $Project -run=UBotDocExport -Plugin=UBotSensor -Out=<uBotSensor>/docs/api.json -MergeZh=Saved/Docs/UBotSensor.zh.json -culture=en -NullRHI

# 引擎节点清单（plugin 为 "Engine"）
& $Editor $Project -run=UBotDocExport -Nodes=Docs/engine-nodes.txt -Out=Saved/Docs/engine.json -culture=en -NullRHI
```

| 参数 | 说明 |
| --- | --- |
| `-Plugin=<Name>` | 导出该插件所有模块（`/Script/<Module>`）中蓝图可见的类、结构体和枚举；`Tests` 目录中的类型不导出。子类改了插件父类中属性的默认值时（例如 IMU 的 `Update Rate Hz` 为 100），该属性在子类里再列一次，带子类的默认值 |
| `-Nodes=<ListFile>` | 导出清单中的节点，与 `-Plugin` 二选一 |
| `-Out=<File>` | 输出文件，UTF-8 无 BOM、LF 换行；同一份源码两次导出逐字节相同 |
| `-MergeZh=<File>` | 同一插件或清单在 `-culture=zh-Hans` 下的导出，用来加入中文字段 |

文字跟随进程的语言（引擎参数 `-culture=<name>`），所以中英双语需要运行两次。不带 `-culture`（也不带 `-language`）时固定导出英文，不会跟随系统或编辑器的语言，文件里的 `"culture"` 是 `"en"`；`-MergeZh` 要求本次运行是英文，并且被合并文件的 `culture` 与之不同（`zh-Hans`）。中文导出与中文编辑器显示的一致：编辑器偏好设置“使用本地化的图表编辑器节点和引脚名称”（`[Internationalization] ShouldUseLocalizedNodeAndPinNames`，默认关闭）关闭时，`Target` 等引脚名和 Make/Break 标题保持英文，只有“目标是 …”这类副标题和引擎函数名是中文。相对路径从工程目录算起。清单文件每行一项，`#` 之后是注释：

```text
/Script/Engine.KismetSystemLibrary:PrintString                              # 函数调用节点
event /Script/Engine.Actor:ReceiveBeginPlay                                  # 可覆盖的事件节点
node /Script/BlueprintGraph.K2Node_IfThenElse                                # 特殊 K2 节点
node /Script/BlueprintGraph.K2Node_GetSubsystem /Script/UBotCore.UBotClockSubsystem  # 某个子系统的 Get 节点
macro /Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop # 宏
```

`node <K2 节点类>` 只接受蓝图编辑器菜单原样提供的节点（Branch、Sequence、Make Array 等）；菜单里只在设置好类型、函数或变量之后才提供的节点（Cast、Make 结构体、Switch on 枚举等）会报错，不会生成。子系统的 Get 节点在节点类后面写子系统类，支持 `K2Node_GetSubsystem`（World、GameInstance、LocalPlayer、AudioEngine 子系统）、`K2Node_GetSubsystemFromPC`（LocalPlayer 子系统）和 `K2Node_GetEngineSubsystem`。它的节点 id 是 `<节点类>:<子系统 C++ 类名>`，例如 `K2Node_GetSubsystem:UUBotClockSubsystem`；标题是类的显示名（`UBot Clock Subsystem`），与图里的紧凑节点一致，菜单里显示为 `Get UBotClockSubsystem`。

退出码：成功为 `0`；参数有误、插件或清单项无法解析、节点无法生成、`-MergeZh` 无法读取、属于其他插件或不是英文导出的中文版本、输出无法写入时为 `1`。
