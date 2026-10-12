# CLAUDE.md

Guide for coding agents and contributors working in this repository (AstreoX/uBotCore).

## What this is

UBotCore is the base Unreal Engine 5.5 plugin of the uBot family. It provides the shared sensor frame header, simulation clock, UE/ROS (REP-103) conventions, sample scheduler, sensor/actuator/perception-medium interfaces, the robot identity component and the uBot package manager basics (version and dependency validation, package index, runtime status, editor panel, commandlets). It depends on engine modules only: no ROS headers, no other uBot plugin. `UBotSensor` and `UBotROS` depend on it (`UBotCore ^0.1.0`). The repo also holds the package index `Index/index.json`, read by uBot Manager, the editor panel and the docs site.

## Layout

| Path | Contents |
| --- | --- |
| `UBotCore.uplugin` | Descriptor: `VersionName`, modules, `UBot` block (`Layer`, `Requires`, `Provides`, `Tags`) |
| `Source/UBotCore/` | Runtime module (`PreDefault`): `Public/` API, `Private/` implementation, `Private/Tests/` tests |
| `Source/UBotCoreEditor/` | Editor module: uBot panel, `UBotPackage` and `UBotDocExport` commandlets, editor settings. `Public/` holds only the module, commandlets, service and settings; `Private/Tests/` holds its tests |
| `Index/` | `index.json`, `index.schema.json`, `README.md` (index format reference) |
| `docs/` | `api.json` (generated, see below), `brand/` logo SVGs used by `README.md` |
| `Resources/`, `Config/FilterPlugin.ini` | Plugin and panel icons; extra files staged when the plugin is packaged |

`README.md` (user documentation) and `Index/README.md` are written in Chinese. Update them when behavior changes and keep their language. Code, comments and commit messages are English. `README.md` images must exist and be listed in `Config/FilterPlugin.ini` (checked by `UBotCore.Package.ReadmeImages`).

## Key types and extension points

- `FUBotFrameHeader`, `EUBotSampleStatus`: `Source/UBotCore/Public/UBotTypes.h`. Field names and enumerator order mirror the legacy Agent* types; do not reorder.
- `UBot::Clock`, `UUBotClockSubsystem` (pause, step, fixed time step): `UBotClock.h`, `UBotClockSubsystem.h` in `Source/UBotCore/Public/`.
- `UBot::Ros` (UE/ROS math) and its Blueprint wrapper `UUBotConventionsLibrary`: `UBotRosConventions.h`, `UBotConventionsLibrary.h`. Keep the pair in sync. `UBot::Units`: `UBotUnits.h`.
- `FUBotSampleScheduler`: `Source/UBotCore/Public/UBotSampleScheduler.h`.
- `IUBotSensor`: `Source/UBotCore/Public/UBotSensorInterface.h`. `IUBotActuator`, `IUBotVelocityCommandable`, `IUBotAttitudeCommandable`, `IUBotMotorCommandable` and their commands (`FUBotVelocityCommand`, `FUBotAttitudeCommand`, `FUBotBodyRateCommand`, `FUBotMotorCommand`): `UBotActuatorInterface.h`. `IUBotPerceptionMedium`, `UBot::Medium`: `UBotPerceptionMedium.h`.
- `UUBotIdentityComponent`, `UBot::Identity`: `Source/UBotCore/Public/UBotIdentityComponent.h`.
- `FUBotExtensionRegistry`, `IUBotExtension` (extension points other packages declare and implement): `Source/UBotCore/Public/UBotExtensionRegistry.h`.
- `FUBotPackageRegistry`: `UBotPackageRegistry.h`. `FUBotPackageInfo` and enums: `UBotPackageTypes.h`. `FUBotSemVer`, `FUBotVersionConstraint`: `UBotSemVer.h`. `UUBotPackageLibrary`: `UBotPackageLibrary.h` (all in `Source/UBotCore/Public/`).
- `FUBotPackageIndex` (parses `Index/index.json`, `SupportedFormatVersion`): `Source/UBotCore/Public/UBotPackageIndex.h`.
- `FUBotRuntimeStatus` (items and problems, writes `Saved/uBot/status.json`): `Source/UBotCore/Public/UBotRuntimeStatus.h`.
- `UUBotCoreSettings` (Project Settings > Plugins > uBot Core): `Source/UBotCore/Public/UBotCoreSettings.h`.
- `UUBotPackageCommandlet`: `Source/UBotCoreEditor/Public/UBotPackageCommandlet.h`. `FUBotPackageService`: `UBotPackageService.h`. `UUBotEditorSettings`: `UBotEditorSettings.h`.
- `UUBotDocExportCommandlet`: `Source/UBotCoreEditor/Public/UBotDocExportCommandlet.h`; implementation in `Private/UBotDocExporter.cpp`.
- Panel texts (English and Simplified Chinese side by side): `Source/UBotCoreEditor/Private/UBotEditorText.h`.

## Build and test

The plugin has no project of its own. Build it inside a host UE 5.5 project that loads it: place or clone it at `<Host>/Plugins/uBot/uBotCore`, or add its parent folder to `AdditionalPluginDirectories` in `<Host>.uproject`, and enable `UBotCore`. Close the host editor before building C++.

```powershell
& '<UE_5.5>\Engine\Build\BatchFiles\Build.bat' <Host>Editor Win64 Development '-Project=<Host>.uproject' -WaitMutex -NoHotReloadFromIDE
& '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<Host>.uproject' -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests UBotCore; Quit' '-TestExit=Automation Test Queue Empty'
```

- Every test name starts with `UBotCore.`. Narrow the run with a longer prefix, e.g. `UBotCore.Packages` or `UBotCore.Editor.DocExport`.
- Tests need the editor context with the plugin enabled; some read the installed plugins and `Index/index.json`.
- Cloud sessions have no Unreal Engine and cannot compile or run tests. Do not claim a build or tests passed. State in the PR that the change is uncompiled and untested, and name the test prefix a maintainer should run.

## Conventions

- Unreal C++: four spaces, PascalCase, `A`/`U`/`F`/`E`/`I` type prefixes, `b`-prefixed booleans, `#pragma once`. Public headers in `Public/` with `UBOTCORE_API` (editor: `UBOTCOREEDITOR_API`), implementation in `Private/`. Log with `LogUBot`.
- Put logic in plain namespaces (`UBot::Ros`, `UBot::Units`, `UBot::Clock`, ...) and keep Blueprint libraries thin wrappers. Public API is SI (meters, seconds, radians) unless the name says `Centimeters` or `Degrees`.
- Tests: `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUBot<Area><Case>Test, "UBotCore.<Area>.<Case>", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)` in `Private/Tests/UBot<Area>Tests.cpp` of the module under test. Reflected helper types for tests go in a `Tests` folder (the docs export skips them). Add or extend a test with each behavior change or fix.
- Blueprint-facing API: `UFUNCTION(BlueprintPure|BlueprintCallable, Category = "uBot|<Area>", meta = (DisplayName = "Title Case Name", Keywords = "..."))`. Areas in use: `Actuation`, `Clock`, `Conventions`, `Frame`, `Identity`, `Medium`, `Packages`, `Sensor` (e.g. `uBot|Clock`). Interfaces are `UINTERFACE(BlueprintType)` with `BlueprintNativeEvent, BlueprintCallable` functions, called through `Execute_*`. Components use `ClassGroup = (uBot)` and `meta = (BlueprintSpawnableComponent, DisplayName = "...")`. Settings derive from `UDeveloperSettings`.
- Write a `/** ... */` comment on every Blueprint-visible class, function and property: it becomes the tooltip in `docs/api.json`. A plain `//` comment goes above `GENERATED_BODY()`, never below it.
- Blueprint-visible names (classes, structs, enums and enumerators, functions and their parameters, properties) are what user Blueprints bind to. Renaming or removing one breaks assets in user projects. Add new API next to the old; to retire one, mark it `meta = (DeprecatedFunction)` and call it out in the PR. Never reorder enumerators.

## docs/api.json

- The Blueprint API description of this plugin (classes, structs, enums, nodes, pins, tooltips; English with Chinese `*Zh` fields). Generated by the `UBotDocExport` commandlet, which lives in the `UBotCoreEditor` module. Never edit it by hand.
- Regenerate after any change to reflected API (types, `UFUNCTION`/`UPROPERTY`, meta, categories, defaults, doc comments) and before tagging a release. It needs a local UE install. A cloud session leaves the file untouched and says in the PR that it needs regeneration.
- Two runs: Chinese first, then English merged with it. Relative paths resolve from the host project directory.

```powershell
$Editor = '<UE_5.5>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
& $Editor '<Host>.uproject' -run=UBotDocExport -Plugin=UBotCore -Out=Saved/Docs/UBotCore.zh.json -culture=zh-Hans -NullRHI
& $Editor '<Host>.uproject' -run=UBotDocExport -Plugin=UBotCore -Out='<this repo>/docs/api.json' -MergeZh=Saved/Docs/UBotCore.zh.json -culture=en -NullRHI
```

- Output is deterministic (UTF-8, LF): a diff without an API change means something is wrong.

## Index/index.json

Package index, `formatVersion` 2, with `packages`, `recipes` and `replacements`. Schema: `Index/index.schema.json`. Field reference: `Index/README.md`. `main` is the live source (uBot Manager downloads it from there), so a merged change is published. uBot Manager embeds a copy; mention index changes in the PR so maintainers can sync it.

```json
{
    "version": "0.1.1",
    "ref": "v0.1.1",
    "engine": "~5.5",
    "requires": [],
    "enginePlugins": [],
    "provides": [ "Core.FrameHeader", "..." ],
    "binaries": { "5.5": { "Win64": { "url": "<release zip>", "sha256": "<64 hex>", "size": 0 } } }
}
```

- Version entries are appended to `versions`; never delete old ones (users pin and downgrade).
- A package or version that is announced but not installable has `"planned": true` and no `ref` or `binaries`. Every other version needs a `ref`.
- `requires`, `provides`, `layer`, `tags` and `enginePlugins` must equal the `UBot` block of the `.uplugin` at that tag, in the same order (`UBotCore.Packages.BuiltInIndex` checks it).
- `ref` is the tag `vX.Y.Z`, pushed before the index mentions it.
- Add `binaries` only after the release zip (`UBotCore-X.Y.Z-UE5.5-Win64.zip`) is uploaded to the GitHub Release of that tag, then push the index. `url`, `sha256` and `size` come from `ubot package ... --update-index`; never invent them. Re-uploading the zip means regenerating the entry.
- Validate with `ubot index validate Index/index.json` (CLI of uBot Manager). If `ubot` is unavailable, check the edit against the schema and say so in the PR.
- Changing the format touches `Source/UBotCore/Public/UBotPackageIndex.h`, `Index/index.schema.json` and `Index/README.md` together.

## Versioning and release

1. Set `VersionName` (SemVer) in `UBotCore.uplugin`. The `.uplugin` at the tag must match the index `version`, or uBot Manager keeps offering an update.
2. Append the index version entry (`ref` = `vX.Y.Z`), regenerate `docs/api.json`, update `README.md`.
3. Tag `vX.Y.Z` and push the tag, upload the zip, then add `binaries` (see above). Maintainers tag and release; do not create tags or releases unless asked.
4. UBotSensor and UBotROS require `UBotCore ^0.1.0`; a `0.2.0` falls outside that range, so keep 0.1.x API-compatible.

## Git

- Never commit `Binaries/`, `Intermediate/`, `Saved/` or `DerivedDataCache/` (see `.gitignore`).
- Conventional Commits: `feat(clock): add ...`, `fix(packages): ...`, `chore(index): ...`, `docs(core): ...`.
- The repo is public: no local paths, credentials, tokens or server addresses in code, docs, tests or logs.
