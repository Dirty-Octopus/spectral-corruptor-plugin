// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

namespace scrr::gui {
// UI preference only: never serialised into a preset or read on the audio thread.
class PromptLanguage
{
public:
    enum class Language { english, chinese };
    inline static juce::File testPreferenceDirectory;
    static juce::File preferenceFile()
    {
        if (testPreferenceDirectory != juce::File()) return testPreferenceDirectory.getChildFile ("language.txt");
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        return base.getChildFile ("SpectralCorruptor/UI/language.txt");
    }
    static Language read (const juce::File& file)
    {
        if (file.getSize() > 32) return Language::english;
        return file.loadFileAsString().trim() == "zh-CN" ? Language::chinese : Language::english;
    }
    static bool write (Language language, const juce::File& file)
    {
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary (file);
        return temporary.getFile().replaceWithText (language == Language::chinese ? "zh-CN" : "en")
            && temporary.overwriteTargetFileWithTemporary();
    }
    static Language get() { return current().load(); }
    static bool set (Language language, bool persist = true)
    {
        current().store (language);
        return ! persist || write (language, preferenceFile());
    }
private:
    static std::atomic<Language>& current()
    { static std::atomic<Language> language { read (preferenceFile()) }; return language; }
};
inline juce::String promptText (const juce::String& english)
{
    if (PromptLanguage::get() == PromptLanguage::Language::english) return english;
    struct Entry { const char* english; const char* chinese; };
    static constexpr Entry entries[] {
        { "Implement your own licensing backend before using this source build.", u8"请先自行实现授权后端，再使用此源码构建版本。" },
        { "Newer Version Available", u8"新版本可用" },
        { "DRAG TO CH / CMD-CTRL: COPY", u8"拖到 CH 移动 / Cmd-Ctrl 拖动复制" },
        { "DRAG TO DETAIL TARGET TO DELETE", u8"拖到右侧标出的删除区并松开以删除" },
        { "DROP OUTPUT ON A PARAMETER", u8"将输出拖到参数上以建立调制" },
        { "SETTINGS", u8"设置" }, { "CLOSE  x", u8"关闭  x" }, { "PROMPT LANGUAGE", u8"提示语言" },
        { "Applies to activation and dialogs. Effect names and controls stay in English.", u8"仅切换激活和提示页面的语言。效果名称与操作控件保持英文。" },
        { "Language saved on this computer. Applies to all plugin instances.", u8"语言设置保存在本机，适用于所有插件实例。" },
        { "SPECTRAL FREQUENCY LIMIT", u8"频谱处理上限" },
        { "UI SCALE", u8"界面缩放" },
        { "UI scale saved on this computer. Applies to all plugin instances.", u8"界面缩放保存在本机，适用于所有插件实例。" },
        { "Could not save UI scale. Check folder permissions.", u8"无法保存界面缩放，请检查文件夹权限。" },
        { "Saved in presets and projects. Higher frequencies bypass spectral effects. GENERIC effects still process the channel.", u8"随预设和工程保存。高于上限的频率绕过频谱效果，GENERIC 效果仍处理整个通道。" },
        { "Could not save language preference. Check folder permissions.", u8"无法保存语言设置，请检查文件夹权限。" },
        { "MACHINE ID UNAVAILABLE", u8"无法读取机器码" },
        { "Paste a code or drop a .sclicense file here", u8"在此粘贴激活码，或拖入 .sclicense 文件" },
        { "Paste a code, enter DEMO, or drop a .sclicense file here", u8"粘贴激活码、输入 DEMO，或拖入 .sclicense 文件" },
        { "DEMO / REMAINING ", u8"DEMO / 剩余 " },
        { "Enter DEMO for 48 hours. Offline exports bypass to dry audio.", u8"输入 DEMO 开始 48 小时试用；离线导出旁通为干声。" },
        { "48-hour demo active. Offline exports bypass to dry audio.", u8"48 小时试用中；离线导出旁通为干声。" },
        { "Enter DEMO to start a 48-hour trial on this machine.", u8"输入 DEMO，在本机开始连续 48 小时试用。" },
        { "48-hour demo is active on this machine.", u8"本机 48 小时试用已启用。" },
        { "The 48-hour demo has expired. Enter your activation code to continue.", u8"48 小时试用已到期，请输入正式激活码继续使用。" },
        { "The 48-hour demo has expired. This demo cannot be restarted.", u8"48 小时试用已到期，无法重新开启。" },
        { "Demo storage is unavailable. Check access to the system credential store and local support folder.", u8"无法访问试用记录，请检查系统凭据存储和本机支持目录的权限。" },
        { "Demo records are missing or invalid. The demo cannot be restarted on this machine.", u8"试用记录缺失或校验失败，无法在本机重新开启试用。" },
        { "Demo records are invalid. This demo cannot be restarted.", u8"试用记录校验失败，无法重新开启。" },
        { "A clock rollback was detected. Demo access is disabled; enter your activation code to continue.", u8"检测到系统时间回退，试用已停用；请输入正式激活码继续使用。" },
        { "A clock rollback was detected. Demo access is disabled.", u8"检测到系统时间回退，试用已停用。" },
        { "Demo credential storage is unavailable or access was denied.", u8"系统凭据存储不可用或访问被拒绝。" },
        { "Demo storage is busy. Try again.", u8"试用记录正忙，请稍后重试。" },
        { "Demo is unavailable in this licensing configuration.", u8"当前授权后端不提供试用模式。" },
        { "Demo is unavailable in the source placeholder.", u8"公开源码占位后端不提供试用模式。" },
        { "Machine code copied.", u8"机器码已复制。" },
        { "ACTIVATED / Normal processing restored.", u8"激活成功 / 已恢复正常音频处理。" },
        { "ACTIVATED / Licence file accepted.", u8"激活成功 / 已导入授权文件。" },
        { "COPY", u8"复制" }, { "ACTIVATE", u8"激活" },
        { "MACHINE ACTIVATION", u8"机器码激活" },
        { "ACTIVATED / THIS MACHINE", u8"此设备已激活" },
        { "NOT ACTIVATED / PINK NOISE ONLY", u8"未激活 / 仅输出粉噪，输入不通过" },
        { "Send this machine code to receive your activation code.", u8"将下方机器码发送给厂商以获取激活码。" },
        { "DROP TO ACTIVATE", u8"松开以激活" },
        { "ACTIVATION CODE / DROP .SCLICENSE", u8"激活码 / 可拖入 .sclicense 文件" },
        { "Unable to read licence file (maximum 4 KB).", u8"无法读取授权文件（最大 4 KB）。" },
        { "Licence file is too large.", u8"授权文件过大。" },
        { "This machine could not be identified.", u8"无法识别此设备。" },
        { "Activation code is too long.", u8"激活码过长。" },
        { "Invalid activation code format.", u8"激活码格式无效。" },
        { "This activation code belongs to another machine.", u8"此激活码属于另一台设备。" },
        { "Invalid activation signature.", u8"激活签名无效。" },
        { "Activation signature did not match.", u8"激活签名不匹配。" },
        { "Could not save activation. Check folder permissions.", u8"无法保存激活信息，请检查文件夹权限。" },
        { "Activated on this machine", u8"此设备已激活" },
        { "Not activated: only pink noise is output; input is blocked.", u8"未激活：仅输出粉噪，输入信号不通过。" },
        { "GENERIC EFFECTS MUST STAY AT THE END OF THE CHAIN", u8"GENERIC 效果只能放在效果链末端，FFT 效果须放在其前方" },
        { "ORDER NOT ALLOWED", u8"无法放置在此位置" }, { "REMOVE", u8"移除效果" },
        { "DROP ON CH TO COPY", u8"拖到 CH 以复制" }, { "RELEASE TO REMOVE", u8"松开以移除效果" },
        { "DROP HERE TO REMOVE", u8"拖到此处以移除效果" },
        { "Outside a target: cancel   /   Esc: cancel", u8"拖到目标区域外取消 / 按 Esc 取消" },
        { "COPY TO CH ", u8"复制到 CH " }, { "MOVE TO CH ", u8"移动到 CH " },
        { "COPY HERE", u8"复制到此处" }, { "INSERT HERE", u8"插入到此处" }, { "NO DROP TARGET", u8"无放置目标" },
        { "Unsaved preset", u8"预设尚未保存" }, { "Save your changes before switching?", u8"是否在切换前保存修改？" },
        { "Keep editing", u8"继续编辑" }, { "Discard and switch", u8"放弃修改并切换" }, { "Save as...", u8"另存为…" },
        { "Save preset", u8"保存预设" }, { "Name this sound", u8"为此预设命名" },
        { "Save", u8"保存" }, { "Cancel", u8"取消" }, { "Invalid preset", u8"无效的预设" },
        { "Preset loaded", u8"预设已载入" }, { "Saved", u8"已保存" },
        { "Random preset generated", u8"随机预设已生成" },
        { "No room for random effects; remove some Notes first.", u8"没有足够位置生成随机效果，请先移除部分 Notes。" },
        { "Export preset", u8"导出预设" }, { "Import preset", u8"导入预设" },
        { "Use a preset name without path separators.", u8"预设名称不能包含路径分隔符。" },
        { "Invalid preset state.", u8"预设数据无效。" },
        { "Could not write preset. Check folder permissions.", u8"无法写入预设，请检查文件夹权限。" },
        { "Could not replace preset. Original file was preserved.", u8"无法替换预设，原文件已保留。" }
    };
    for (const auto& entry : entries)
        if (english == entry.english) return juce::String::fromUTF8 (entry.chinese);
    return english;
}
}
