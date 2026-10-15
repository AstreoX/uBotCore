# 开发

下面的内容原样移自重写前的 README，文档站（https://astreox.github.io/uBotDocs/）上对应的页面没有覆盖其中的细节。

## 模块

| 模块 | 类型 | 加载阶段 | 说明 |
| --- | --- | --- | --- |
| `UBotCore` | Runtime | PreDefault | 运行时 API |
| `UBotCoreEditor` | Editor | Default | uBot 面板、PIE 会话记录、`UBotPackage` 与 `UBotDocExport` commandlet |

## 测试

测试名都以 `UBotCore.` 开头，分类为 `Clock`、`Conventions`、`Editor.DocExport`、`Extensions`、`Identity`、`Interfaces`、`Medium`、`PackageManager`、`Packages`、`Panel`、`RuntimeStatus`、`Scheduler`。`Editor.DocExport`、`PackageManager` 与 `Panel` 测试在 `UBotCoreEditor` 模块里。测试需要在启用了 UBotCore 的工程中，用带编辑器上下文的 `UnrealEditor` 运行（`Packages.RegistryFindsCore` 等会读取已安装的插件）。

编辑器中：打开 Session Frontend（`Tools` 菜单）的 Automation 标签页，过滤 `UBotCore`。

命令行：

```powershell
& '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<Project>\<Project>.uproject' -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests UBotCore; Quit' '-TestExit=Automation Test Queue Empty'
```

把 `UBotCore` 换成更具体的前缀（如 `UBotCore.Packages`）可以只跑一类。
