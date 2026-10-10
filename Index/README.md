# uBot Package 索引

`index.json` 是 uBot 家族的 Package 索引（格式 2）：列出所有已知的 uBot Package（包括规划中、还不能安装的）、常用的 Recipe，以及被 uBot Package 取代的旧插件。`index.schema.json` 是它的 JSON Schema（draft 2020-12），在文件开头写 `"$schema": "./index.schema.json"` 即可让编辑器检查结构；名称唯一、依赖存在、没有依赖循环、仓库地址策略等跨条目的规则由 `ubot index validate` 检查（见下文“校验”）。

## 谁读取它

| 读取方 | 读哪里 | 用来做什么 |
| --- | --- | --- |
| uBot Manager | 若干索引源，见下一节 | 列出 Package，解析依赖，安装（克隆，或下载预编译包）、更新，应用 Recipe，报告旧插件冲突 |
| UE 编辑器（`FUBotPackageIndex::LoadConfiguredIndex`） | `<Project>/Saved/uBot/index.json`；不存在时读已安装的 UBotCore 里的 `Index/index.json`；然后是 `Project Settings > Plugins > uBot Core` 中的 `AdditionalPackageIndexFiles` | uBot 面板的更新提示和旧插件冲突，commandlet 的 `-List`、`-Install` |

## 索引源与回退

uBot Manager 按“设置 > Package 索引 > 索引源”里的顺序读取索引源。默认有两个内置源，不能移除或禁用，可以调整顺序：

| 索引源 | 位置 | 说明 |
| --- | --- | --- |
| uBotCore | `https://raw.githubusercontent.com/AstreoX/uBotCore/main/Index/index.json` | 通过 HTTPS 下载，超时 10 秒，遵循 `HTTPS_PROXY` / `HTTP_PROXY`。成功后缓存为 `%LOCALAPPDATA%\AstreoX\uBot Manager\cache\index-ubot.json` |
| 本地副本 | 当前工程里已安装的 UBotCore 的 `Index/index.json`，显示为工程相对路径，如 `Plugins/uBot/uBotCore/Index/index.json` | 没有安装 UBotCore（或它没有这个文件）时不可用，不算错误 |

可以再添加自己的索引源（团队镜像、内部 Package）：`https://` 地址，或文件路径（绝对路径，或相对于工程目录）。纯 `http://` 只允许本机地址（localhost 上的测试服务器）；下载时的重定向也只跟随 `https://`。每个索引文件最大 8 MiB。

规则：

- **合并**：按列表顺序，**第一个**定义某个 Package（不区分大小写）的源决定这个 Package 的全部内容，不会逐字段混合；Recipe 按 `id` 同样处理；`replacements` 取并集，同一个旧插件以第一个为准。把自己的源放在前面，就能覆盖官方条目。
- **回退**：读取失败的源被跳过，所以“远程 → 本地副本”的顺序本身就是回退顺序。远程源下载失败但有缓存时使用缓存（界面里标注“缓存”），下载错误仍会列在 Problems 里。
- **内置副本**：没有任何源能提供索引（读取失败又没有缓存，或不可用）时，使用编译进 uBot Manager 的一份 UBotCore `Index/index.json`，状态行显示为“内置副本”。
- **何时下载**：设置 `启动时同步索引`（默认开）时，启动后重新下载远程源；否则只在没有缓存时下载。标题栏的刷新按钮随时手动同步。
- **写给 UE**：有打开的工程时，每次刷新后 uBot Manager 把合并结果以格式 2 写到 `<Project>/Saved/uBot/index.json`（`name` 为 `merged`）。其中的仓库地址保持原样，包括地址里可能带的凭据，所以不要提交 `Saved/`；`binaries` 里的地址已去掉凭据、查询串和片段；界面、日志和命令行输出里的凭据都显示为 `***`。命令行只有 `apply` 和 `recipe` 会写这个文件。

命令行 `ubot --index <文件或URL>`（可重复）替换配置里的全部索引源，`--offline` 只用缓存。

## 格式

键名用 camelCase（`.uplugin` 里的键仍是 UE 要求的 PascalCase）。读取方忽略不认识的键，`ubot index validate` 会对它们给出警告。

```jsonc
{
    "$schema": "./index.schema.json",
    "formatVersion": 2,
    "name": "uBot",
    "packages": [
        {
            "name": "UBotSensor",
            "friendlyName": "uBot Sensor",
            "description": { "en": "...", "zh-CN": "..." },
            "layer": "Capability",
            "repository": "https://github.com/AstreoX/uBotSensor.git",
            "docsUrl": "https://github.com/AstreoX/uBotSensor",
            "tags": [ "sensor" ],
            "versions": [
                {
                    "version": "0.1.0",
                    "ref": "v0.1.0",
                    "engine": "~5.5",
                    "requires": [ { "name": "UBotCore", "version": "^0.1.0" } ],
                    "enginePlugins": [],
                    "provides": [ "Sensor.IMU" ]
                }
            ]
        }
    ],
    "recipes": [ ... ],
    "replacements": [ ... ]
}
```

### 顶层

| 字段 | 说明 |
| --- | --- |
| `$schema` | 可选，读取时忽略 |
| `formatVersion` | 必填，数字 `2`；其他值一律拒绝。写成 `FormatVersion: 1` 的旧文件会得到“已不再支持”的提示 |
| `name` | 这份索引的名称，仅作标识，界面不显示；缺少时校验给出警告。uBot Manager 合并后的索引叫 `merged` |
| `packages` | 必填，数组 |
| `recipes`、`replacements` | 可选，数组 |

### Package

| 字段 | 说明 |
| --- | --- |
| `name` | 必填，UE 插件名（字母、数字和 `_`，不以数字开头），不区分大小写唯一 |
| `friendlyName` | 显示名，缺省为 `name` |
| `description` | 按语言存放的文字：`en` 必填，`zh-CN` 建议填写（缺少只是警告）。UE 在中文界面（`zh`、`zh-Hans`、`zh-CN` 等）用 `zh-CN`，其余及缺失时用 `en` |
| `layer` | `Foundation`、`Capability`、`Adapter` 或 `Content`，与 `.uplugin` 里的 `Layer` 一致；缺少是警告，无法识别是错误 |
| `repository` | git 仓库地址。有可安装版本时必填，全部版本都是规划版本时可省略。地址策略见下 |
| `folder` | 可选，安装目录名：装到 `<工程>/Plugins/uBot/<folder>`（目录前缀来自 uBot Manager 设置里的“安装目录”和 UE 的 `PackageInstallDirectory`，默认都是 `Plugins/uBot`）。缺省取仓库地址最后一段并去掉 `.git`。必须是单个路径段，只含字母、数字、`.`、`_`、`-`，不能是 `.` 或 `..` |
| `docsUrl` | 可选，文档地址 |
| `tags` | 可选，字符串数组 |
| `versions` | 必填，至少一项，顺序任意（读取方按 SemVer 排序） |

仓库地址策略（uBot Manager 与 UE 的 commandlet 相同）：允许 `https://`、`ssh://`、`file://`、`user@host:path` 和绝对本地路径（团队镜像、测试），`http://` 可用但校验会警告；拒绝 `git://` 等其他协议、相对路径、以 `-` 开头的地址、`ext::` 这类 `<transport>::` 形式，以及含空白、引号或控制字符的地址。

### 版本

| 字段 | 说明 |
| --- | --- |
| `version` | 必填，SemVer。接受 `1`、`1.2`、`1.2.3`，可带前缀 `v` 和 `-prerelease`，`+build` 被忽略。同一 Package 内按 SemVer 比较后不能重复（`0.1` 和 `0.1.0` 是同一个版本）。只能含字母、数字、`.`、`-` 和 `+`，因为它会成为预编译包的文件名和下载地址的一部分 |
| `ref` | 要检出的 git 引用，通常是标签；也可以是 origin 上的分支或提交 ID。除规划版本外必填。不能以 `-` 开头，不能含空白、`..`，也不能含 `~ ^ : ? * [ \ "`。发布版本应使用标签，分支会随推送移动 |
| `planned` | `true` 表示已公布但还不能安装，此时不写 `ref`（写了会被忽略，校验给出警告） |
| `engine` | 可选，对 UE 版本的约束，语法同下面的版本约束，例如 `~5.5`（`>=5.5.0 <5.6.0`）。工程的引擎不满足时，uBot Manager 在计划里给出警告，不阻止安装；UE 只检查它能否解析 |
| `requires` | 依赖列表，每项 `name`（必须是本索引里的 Package）、`version`（版本约束，缺省为任意版本）、`optional`（默认 `false`）。可选依赖不会被自动安装，也不构成依赖边 |
| `enginePlugins` | 必须启用的引擎插件名，如 `ChaosVehiclesPlugin`。uBot Manager 会在计划里把缺失的写入 `.uproject`，引擎里没有这个插件则计划报错；UE 的 commandlet 不会安装或启用它们，只在缺失或未启用时给出提示 |
| `provides` | 能力 ID，如 `Sensor.IMU`，显示在详情里，C++ 中是 `FUBotPackageInfo::Provides` |
| `binaries` | 可选，这个版本的预编译压缩包，按 UE 版本和平台给出下载地址与 SHA-256，见下文“预编译二进制 `binaries`” |

版本约束（`requires[].version`、`engine`）的语法与 `.uplugin` 的 `Requires[].Version` 相同：`^`、`~`、比较符、X-range，多个比较器用空格分隔，见 UBotCore 的 README。

**最新版本**是最高的非规划版本。没有非规划版本的 Package 是**规划中**的：会显示，但不能安装（UE 中 `bPlanned` 为 `true`）。`requires`、`provides` 和 `enginePlugins` 按版本写；Package 没有安装时，uBot Manager 和 UE 用最新版本（规划中的 Package 用最新的规划版本）的那一份。已安装的 Package 以它自己的 `.uplugin` 为准。

**`binaries`** 让 uBot Manager 直接下载编译好的插件，而不是克隆再编译。它可以是 `null` 或省略，表示这个版本没有预编译版本。格式见下面的“预编译二进制 `binaries`”。

### 预编译二进制 `binaries`

`binaries` 是某个版本的预编译压缩包：先按 UE 版本，再按平台。

```json
"binaries": {
    "5.5": {
        "Win64": {
            "url": "https://github.com/AstreoX/uBotSensor/releases/download/v0.1.0/UBotSensor-0.1.0-UE5.5-Win64.zip",
            "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "size": 12345678
        }
    }
}
```

| 位置 | 说明 |
| --- | --- |
| 第一层的键 | UE 版本，只写 `主版本.次版本`，如 `5.5`，不写补丁号（`5.5.4` 不是合法的键） |
| 第二层的键 | 平台。只认识 `Win64`，其他平台被忽略（校验给出警告） |
| `url` | 必填。`https://` 地址、`file://` 地址或绝对本地路径（团队镜像、测试）。除了仓库地址策略的限制，还不允许纯 `http://`、`ssh://` 和 `user@host:path`，`https://` 地址里也不能有反斜杠 |
| `sha256` | 必填。压缩包的 SHA-256，64 位小写十六进制 |
| `size` | 可选。压缩包的精确字节数，正整数，最大 2 GiB。写了就严格执行：下载到的字节数必须相同 |

读取方的规则：

- uBot Manager 在下面三条同时满足时才安装预编译版本：设置里的“有预编译版本时优先使用”已勾选（默认勾选）；项目的引擎是 Epic 启动器安装的（源码构建的引擎自己编译）；这个版本有 `binaries[<引擎的主版本.次版本>]["Win64"]`。否则照旧克隆并编译。已装的 git 检出更新时仍然用 git，不会换成预编译版本。
- 规划版本没有预编译版本；写了会得到警告。
- 一条写错的 `binaries` 不会让整个索引无法读取，uBot Manager 只是不使用它，`ubot index validate` 会报错。
- 写入 `<Project>/Saved/uBot/index.json` 的合并索引里，`binaries` 的地址去掉了凭据、查询串和片段（签名下载链接的令牌在查询串里）。
- UE 不读取 `binaries`，也不检查它。

压缩包的布局（`ubot package` 就是这样做的）：

- 只有一个顶层文件夹，名字是插件名（`UBotSensor/`），里面是 `UBotSensor.uplugin`、`Binaries/Win64`（`UnrealEditor-*.dll` 和 `UnrealEditor.modules`）、`Source`、`Resources`、`Config` 等。
- 没有 `.git`、`Intermediate`、`Saved` 和 `DerivedDataCache`；没有 `*.pdb`、`*.exp`、`*.lib`，除非打包时加了 `--with-symbols`。
- 文件名约定为 `<Name>-<版本>-UE<主版本.次版本>-Win64.zip`，如 `UBotSensor-0.1.0-UE5.5-Win64.zip`，旁边有同名加 `.sha256` 的文本文件。
- 插件文件夹的顶层不能有以 `.ubot-` 开头的内容，包括安装标记 `.ubot-install.json`：标记由 uBot Manager 安装后自己写，不取自压缩包。
- 插件有 C++ 模块时，`Binaries/Win64` 必须有 `UnrealEditor-*.dll` 和 `UnrealEditor.modules`，缺了安装会失败；`.uplugin` 的 `VersionName` 必须等于索引里的 `version`。

uBot Manager 下载、校验 SHA-256、安全解压（拒绝绝对路径、`..`、符号链接、`.git` 等）、检查版本，然后写入标记并替换文件夹；具体过程见 uBot Manager 的 README。预编译包里只有编辑器用的二进制，打包游戏时 UE 仍会用 `Source` 编译插件，需要 Visual Studio 2022。

#### 制作和发布预编译版本

压缩包和 `binaries` 条目由 uBot Manager 的命令行 `ubot package` 生成（`ubot` 怎么得到，见下文“校验”）：它在临时的纯内容工程里按索引里的 `ref` 克隆并编译指定的 Package 和它们的依赖，把每个指定的 Package 打成 zip，并把条目写进索引文件。需要 git、启动器版本的引擎（源码构建的引擎会被拒绝）和 Visual Studio 2022。

1. 先按上文把版本发布出来：`.uplugin` 的 `VersionName`、标签 `v<VersionName>`（已推送）、索引里追加这个版本（有 `ref`，暂时没有 `binaries`）。`ubot package` 从索引里取版本和 `ref`。
2. 打包并写入索引。版本还没有出现在 `main` 上，所以用 `--index` 指向本地的 `Index/index.json`：

   ```powershell
   ubot --index Index/index.json package UBotCore UBotSensor UBotROS --out E:\release --update-index Index/index.json
   ```

   只打包列出的 Package，依赖不会自动打包；`--engine` 省略时用最新的启动器引擎，也可以写 `--engine 5.5`。输出里有每个 Package 的条目，同时写进索引：只改对应版本的 `binaries["5.5"]["Win64"]`，文件的其他部分保持原有的键顺序和格式（新写入的部分用 4 空格缩进），写完会重新校验。
3. 把 `E:\release` 里的 zip 上传到各自仓库、对应标签的 GitHub Release。默认的 `url` 模板是 `https://github.com/AstreoX/{repo}/releases/download/{ref}/{file}`，即 `AstreoX/<仓库名>` 这个仓库里标签为 `<ref>` 的 Release 下的文件；换了托管地方时用 `--url-template` 改（占位符 `{repo}`、`{ref}`、`{file}`、`{name}`、`{version}`、`{engine}`）。
4. 校验索引（`ubot index validate Index/index.json`），再提交并推送 uBotCore。先上传、后推送索引：索引一推送，用户就会开始下载；压缩包重新上传过（字节变了）就必须重新写入条目，否则 SHA-256 和 `size` 对不上。
5. 在 uBot Manager 仓库运行 `node scripts/sync-index.mjs` 更新内置副本。

为别的 UE 版本制作预编译版本：换用那个版本的启动器引擎再运行一次 `ubot package --engine 5.6`，条目会加到 `binaries["5.6"]`，已有的 `5.5` 条目保留。

### Recipe

Recipe 是一组常用的 Package，用来一次装好一个场景。

| 字段 | 说明 |
| --- | --- |
| `id` | 必填，唯一（不区分大小写），字母、数字、`.`、`_`、`-` |
| `name`、`description` | 按语言存放的文字，规则同 Package 的 `description`：`en` 必填，`zh-CN` 建议填写 |
| `packages` | 至少一项，每项必须是本索引里的 Package 名 |
| `checks` | 环境检查的 ID，目前只认识 `rosBridge`，其他值被忽略（校验给出警告） |

- 所有 `packages` 都在索引里且不是规划中时，Recipe 才可用；否则不可用，`ubot recipe <id>` 会报错。
- 应用 Recipe 只做两件事：安装缺失的 Package，启用已禁用的 Package；依赖由计划自动补齐。不会更新、禁用或卸载已有的 Package。
- “应用 Recipe”对话框的“环境要求”固定检查 Git 和 Visual Studio 2022，再加上 `checks` 里的项。`rosBridge` 对应 `ubot_ros_bridge`，它运行在 ROS 一侧，对话框只提示，不检查。
- 用户在“应用更改”对话框里勾选“保存为 Recipe”后，当前启用的 uBot Package 会存成自己的 Recipe。它保存在 uBot Manager 的 `settings.json`（`userRecipes`），不写进索引。
- UE 不读取 Recipe。

### replacements

`replacements` 记录被 uBot Package 取代的旧插件：

```json
{ "legacy": "AgentSensorCore", "replacement": "UBotSensor", "redirects": "Config/DefaultUBotSensor.ini" }
```

| 字段 | 说明 |
| --- | --- |
| `legacy` | 旧插件名，不区分大小写唯一 |
| `replacement` | 取代它的 Package，必须在本索引里 |
| `redirects` | 可选，替代 Package 里存放 CoreRedirects 的配置文件，相对 Package 目录，用 `/`，不能含 `..`、反斜杠，也不能是绝对路径 |

旧插件（没有 `"UBot"` 块的插件）和 `replacement` 同时启用时，uBot Manager 报告一个警告（快速修复：禁用旧插件，或忽略），详情里显示 `redirects`；UE 的 uBot 面板在 Problems 里显示同一个冲突。`redirects` 只用于显示，uBot Manager 不会读取或修改那个文件。

## 添加 Package 或新版本

索引里写的每一项，都要和 Package 仓库里的 `.uplugin` 保持一致。没有安装时 uBot Manager 按索引规划，安装后按 `.uplugin` 规划，两边不一致就会得到不同的结果。

**新 Package**

1. 在 Package 仓库的 `.uplugin` 里写好 `VersionName` 和 `"UBot"` 块（`Layer`、`Repository`、`Requires`、`Provides`、`Tags`，需要时加 `EnginePlugins`），提交。
2. 给发布的提交打标签 `v<VersionName>` 并推送：

   ```powershell
   git tag v0.1.0
   git push origin v0.1.0
   ```

   标签命名统一为 `v` 加版本号，例如版本 `0.1.0` 的标签是 `v0.1.0`。标签必须先推到仓库，再写进索引：uBot Manager 用 `git clone --branch <ref>` 安装，更新时先 `git fetch --tags origin` 再检出。

3. 在 `Index/index.json` 的 `packages` 里加一项，字段取自 `.uplugin`：

   | 索引 | `.uplugin` |
   | --- | --- |
   | `versions[].version` | `VersionName`（该标签处的值） |
   | `layer` | `UBot.Layer` |
   | `repository` | `UBot.Repository` |
   | `versions[].requires` | `UBot.Requires`（`name` ↔ `Name`，`version` ↔ `Version`，`optional` ↔ `Optional`，顺序也要一致） |
   | `versions[].provides` | `UBot.Provides`（顺序一致） |
   | `tags` | `UBot.Tags`（顺序一致） |
   | `versions[].enginePlugins` | `UBot.EnginePlugins` |
   | `friendlyName`、`docsUrl` | `FriendlyName`、`DocsURL` |

   示例（`UBotSonar` 是假想的 Package）：

   ```json
   {
       "name": "UBotSonar",
       "friendlyName": "uBot Sonar",
       "description": { "en": "Simulated sonar sensor.", "zh-CN": "声呐传感器仿真。" },
       "layer": "Capability",
       "repository": "https://github.com/AstreoX/uBotSonar.git",
       "docsUrl": "https://github.com/AstreoX/uBotSonar",
       "tags": [ "sensor" ],
       "versions": [
           {
               "version": "0.1.0",
               "ref": "v0.1.0",
               "engine": "~5.5",
               "requires": [
                   { "name": "UBotCore", "version": "^0.1.0" },
                   { "name": "UBotSensor", "version": "^0.1.0" }
               ],
               "enginePlugins": [],
               "provides": [ "Sensor.Sonar" ]
           }
       ]
   }
   ```

   对应的 `.uplugin` 片段：

   ```json
   "VersionName": "0.1.0",
   "UBot": {
       "Layer": "Capability",
       "Repository": "https://github.com/AstreoX/uBotSonar.git",
       "Requires": [
           { "Name": "UBotCore", "Version": "^0.1.0" },
           { "Name": "UBotSensor", "Version": "^0.1.0" }
       ],
       "Provides": [ "Sensor.Sonar" ],
       "Tags": [ "sensor" ]
   }
   ```

4. 需要时把它加进 `recipes`；如果它取代了某个旧插件，在 `replacements` 里加一项。
5. 校验并发布，见下文。

**新版本**

1. 修改 `.uplugin` 的 `VersionName`（以及 `UBot` 块），提交，打标签 `v<VersionName>` 并推送。
2. 在该 Package 的 `versions` 里**追加**一项，保留旧版本：用户需要降级或固定版本，UE 的测试也要求已安装的版本仍在索引里。新版本的 `requires`、`provides`、`enginePlugins` 取自这个标签处的 `.uplugin`。
3. 最新版本就是最高的非规划版本；已安装更低版本的工程，在 uBot Manager 里会看到“有更新”，UE 面板的 Loaded Packages 里也会显示可更新到的版本。uBot Manager 拿安装后的 `VersionName` 和索引里的最新版本比较，所以标签处 `.uplugin` 的 `VersionName` 必须等于 `version`；低于它的话，安装之后会一直显示有更新。
4. 新版本超出下游 Package 的依赖范围时（例如 UBotCore 发布 `0.2.0`，而 UBotSensor 要求 `^0.1.0`），uBot Manager 会拒绝单独更新 UBotCore。要同时发布放宽了约束的下游新版本，用户把它们放进同一个计划里一起更新。
5. 把规划版本变成正式版本：去掉 `"planned": true`，加上 `ref`。
6. 需要预编译版本时，按“制作和发布预编译版本”给这个版本加上 `binaries`。没有 `binaries` 的版本仍然可以安装，只是要克隆并编译。

**发布**

1. 校验（`ubot index validate Index/index.json`）；在装有这些 Package 的工程里跑 `UBotCore.Packages.BuiltInIndex` 测试（见 UBotCore 的 README“测试”一节）。
2. 如果索引里有新的 `binaries`，确认压缩包已经上传、地址能下载。
3. 提交并推送 uBotCore 的 `main`：远程源读取的就是这个分支，uBot Manager 在下次同步时看到变化。
4. 在 uBot Manager 仓库运行 `node scripts/sync-index.mjs`，更新内置副本（`crates/ubot-core/assets/index.json`）；`--check` 只比较不写入，适合放进 CI。

## 校验

```powershell
ubot index validate Index/index.json
```

`ubot` 是 uBot Manager 仓库里的命令行程序（`cargo build --release -p ubot-cli`，得到 `target/release/ubot.exe`）。`validate` 只读指定的文件，不需要工程，也不访问网络。输出是英文，加 `--json` 得到 `{ "ok": ..., "errors": [...], "warnings": [...] }`。有错误时退出码为 1，只有警告时为 0。

```text
error: packages[0] (UBotFoo).versions[0]: "ref" is required for a version that is not planned.
error: packages[0] (UBotFoo).versions[0].requires[0]: 'UBotBar' is not in this index.
Index/index.json: 2 error(s), 0 warning(s).
```

错误：

- 文件不是合法 JSON，或字段类型不对（这时整个文件只报一个错误，并指出出错的条目）；`formatVersion` 不是 2。
- Package：名称不是合法插件名或重复；`layer` 无法识别；`description.en` 为空；有可安装版本却没有 `repository`；`repository` 违反地址策略；安装目录名（显式的 `folder` 或推导出来的）不安全，或无法推导；`versions` 为空。
- 版本：`version` 无法解析或重复，或含有字母、数字、`.`、`-`、`+` 以外的字符；非规划版本没有 `ref`，或 `ref` 不安全；`engine` 或 `requires[].version` 无法解析；`requires` 引用了本索引里没有的 Package；`enginePlugins` 里有非法的插件名。
- `binaries`：不是对象（`null` 可以）；第一层的键不是 `主版本.次版本`；某个引擎的值不是平台对象；`Win64` 条目不是对象，`url` 缺失、不是字符串或违反地址策略（`http://`、`ssh://`、相对路径、`https://` 里的反斜杠等），`sha256` 不是 64 位小写十六进制，`size` 不是正整数或超过 2 GiB。
- 依赖循环：用每个 Package 最新（规划中则最新规划）版本的非可选依赖检查。
- Recipe：`id` 为空或重复；`name.en` 或 `description.en` 为空；`packages` 为空，或引用了不在索引里的 Package。
- replacements：`legacy` 为空或重复；`replacement` 不在索引里；`redirects` 不是相对的 `/` 路径。

警告：缺少 `name`、`layer`、`description`，缺少 `zh-CN` 文本（Package、Recipe），`repository` 使用 `http://`，两个 Package 安装到同一个文件夹，规划版本写了 `ref` 或 `binaries`，`binaries` 里 `Win64` 以外的平台（会被忽略），未知的 Recipe `checks`，以及不认识的键（`binaries` 条目里的也算）。

这个命令不比较索引和 `.uplugin`。那部分由 UE 的自动化测试 `UBotCore.Packages.BuiltInIndex` 检查：本目录的 `index.json` 能被干净地读取；已安装的 `UBot*` Package 的版本在索引里，并且当它就是最新版本时，`provides`、`tags`、`layer`、`enginePlugins` 和 `requires` 与它的 `.uplugin` 一致；所有条目放在一起能通过依赖校验；每个 `replacement` 都在索引里。

## UE 读取时的差异

UE 逐条读取：损坏的条目（不是对象、没有 `name`、重名、没有可用的版本）被跳过并报告，其余照常加载；格式有问题的可选字段只报告、不影响条目。报告出现在 uBot 面板的 Problems 里（`Package 索引：…`）和日志里。uBot Manager 则要求整个文件的结构正确，否则这个索引源失败并被跳过。

UE 不校验仓库地址（克隆时才校验），不读取 `recipes`，`engine` 只检查能否解析，`binaries` 不读取，也不检查：UE 用的是磁盘上的插件，不管它是克隆来的还是下载来的。

## 从格式 1 转换

格式 1（早期 UBotCore 中的 `Resources/PackageIndex.json`）已不再支持，UE 读到时会报错。对应关系：

| 格式 1 | 格式 2 |
| --- | --- |
| `FormatVersion: 1` | `formatVersion: 2` |
| `Packages[]` | `packages[]` |
| `Name`、`FriendlyName`、`Layer`、`Repository`、`DocsUrl`、`Tags` | 同名的 camelCase 键 |
| `Description`（字符串） | `description.en`（可加 `zh-CN`） |
| `Version`、`Requires`、`Provides`、`ExternalRequires` | `versions[]` 中的一项：`version`、`requires`、`provides`、`enginePlugins`，并加上 `ref` |
