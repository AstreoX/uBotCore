#pragma once

#include "CoreMinimal.h"

/**
 * Every user-facing text of the uBot editor panel: key, English source text and Simplified Chinese
 * (wording from the UE panel design and the uBot terminology rules; "Package" stays English).
 * Each entry becomes a LOCTEXT accessor in UBot::EditorText (namespace "UBotEditor") and a zh-Hans
 * translation registered by RegisterChineseText(), so the two cannot drift apart.
 */
#define UBOT_EDITOR_TEXTS(Entry) \
    Entry(TabTitle, "uBot", "uBot") \
    Entry(TabTooltip, "uBot packages, extension points, runtime status and problems", "uBot Package、扩展点、运行时状态和问题") \
    Entry(OpenPanelTooltip, "Open the uBot panel", "打开 uBot 面板") \
    Entry(OpenManager, "Open uBot Manager", "打开 uBot Manager") \
    Entry(OpenManagerTooltip, "Install, update and configure uBot packages in uBot Manager", "在 uBot Manager 中安装、更新和配置 uBot Package") \
    Entry(Refresh, "Refresh", "刷新") \
    Entry(SectionPackages, "Loaded Packages", "已加载的 Package") \
    Entry(SectionExtensionPoints, "Extension Points", "扩展点") \
    Entry(SectionRuntime, "Runtime", "运行时") \
    Entry(SectionProblems, "Problems", "问题") \
    Entry(UpdateAvailable, "{0} · {1} available", "{0} · 可更新到 {1}") \
    Entry(Implementations, "{0} {0}|plural(one=implementation,other=implementations)", "{0} 个实现") \
    Entry(SimTime, "Sim time", "仿真时间") \
    Entry(SimTimeValue, "{0} s", "{0} 秒") \
    Entry(FixedTimeStep, "Fixed time step", "固定时间步长") \
    Entry(FixedTimeStepOff, "Off", "关") \
    Entry(FixedTimeStepRate, "{0} Hz", "{0} Hz") \
    Entry(LastSession, "Last session", "上次运行") \
    Entry(ProblemRequires, "{0} requires {1}", "{0} 需要 {1}") \
    Entry(ProblemRequiresMissing, "{0} requires {1}, which is not installed", "{0} 需要 {1}，但尚未安装") \
    Entry(ProblemRequiresDisabled, "{0} requires {1}, which is disabled", "{0} 需要 {1}，但它已禁用") \
    Entry(ProblemOfPackage, "{0}: {1}", "{0}：{1}") \
    Entry(ProblemLegacy, "{0} and {1} both enabled", "{0} 和 {1} 同时启用") \
    Entry(ProblemIndex, "Package index: {0}", "Package 索引：{0}") \
    Entry(StatusBar, "uBot: {Packages} {Packages}|plural(one=package,other=packages) · {Problems} {Problems}|plural(one=problem,other=problems)", "uBot：{Packages} 个 Package · {Problems} 个问题") \
    Entry(ManagerNotFound, "uBot Manager not found", "找不到 uBot Manager") \
    Entry(ManagerNotFoundHint, "Install it, or set its path in Editor Preferences > Plugins > uBot.", "请安装 uBot Manager，或在“编辑器偏好设置 > 插件 > uBot”中设置它的路径。") \
    Entry(ManagerDownload, "Download uBot Manager", "下载 uBot Manager") \
    Entry(ManagerLaunchFailed, "Could not start uBot Manager", "无法启动 uBot Manager")

namespace UBot::EditorText
{
#define UBOT_DECLARE_EDITOR_TEXT(Key, English, Chinese) FText Key();
    UBOT_EDITOR_TEXTS(UBOT_DECLARE_EDITOR_TEXT)
#undef UBOT_DECLARE_EDITOR_TEXT

    /** Registers the zh-Hans translations with the text localization manager (editor texts follow the editor language). */
    void RegisterChineseText();
}
