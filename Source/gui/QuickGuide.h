// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
namespace scrr::gui {
class QuickGuide : public juce::Component
{
public:
    QuickGuide()
    {
        setComponentID ("quick-guide"); body.setMultiLine (true); body.setReadOnly (true); body.setScrollbarsShown (true);
        body.setCaretVisible (false); body.setPopupMenuEnabled (false); body.setFont (Theme::font (15));
        addAndMakeVisible (body); addAndMakeVisible (previous); addAndMakeVisible (next); addAndMakeVisible (done); addAndMakeVisible (never);
        previous.onClick = [this] { page = juce::jmax (0, page - 1); refresh(); };
        next.onClick = [this] { page = juce::jmin (3, page + 1); refresh(); };
        done.onClick = [this]
        {
            {
                auto f = preference(); if (f.getParentDirectory().createDirectory().failed()) return;
                juce::TemporaryFile tmp (f);
                if (! tmp.getFile().replaceWithText (never.getToggleState() ? "hidden" : "show") || ! tmp.overwriteTargetFileWithTemporary())
                { body.setText (text ("Could not save this preference. Please try again.", u8"无法保存设置，请重试。")); return; }
            }
            setVisible (false);
        };
        refresh();
    }
    static juce::File preference() { return PromptLanguage::preferenceFile().getSiblingFile ("guide.txt"); }
    static bool shouldShow() { return preference().loadFileAsString().trim() != "hidden"; }
    void open() { page = 0; never.setToggleState (! shouldShow(), juce::dontSendNotification); refresh(); setVisible (true); toFront (true); }
    void refresh()
    {
        previous.setButtonText (text ("BACK", u8"上一步")); next.setButtonText (text ("NEXT", u8"下一步"));
        done.setButtonText (text ("GOT IT", u8"确认")); never.setButtonText (text ("Do not show again", u8"以后不再显示"));
        previous.setEnabled (page > 0); next.setEnabled (page < 3);
        static const char* en[] {
            "01 / BUILD YOUR SOUND\n\nSpectral Corruptor is an audio effect: feed it audio from your DAW.\n\nUse + ADD EFFECT to browse the effect folders. Select a row in the CH SIGNAL CHAIN to edit it. LOW / HIGH choose its spectral range; STAGE MIX blends its result. GENERIC time-domain effects stay at the end of a channel.\n\nThe dot beside each CH tab mutes that channel. SOLO auditions the selected channel; POST SPLIT uses its assigned frequency slices.",
            "02 / DRAG, COPY & REMOVE\n\nDrag the handle of a chain module to reorder it. Hover over another CH tab to preview that channel before dropping. Hold Cmd (macOS) or Ctrl (Windows) while dragging to copy.\n\nTo DELETE: drag the module into the marked removal target in the right-hand effect detail area, then release. The entire detail panel is not a delete target. Drop outside a target or press Esc to cancel.\n\nUndo / Redo and Cmd/Ctrl+Z are available. Right-click a module for its copy/paste actions.",
            "03 / MODULATION & MACROS\n\nOpen MODULATION. Add LFO, Random or Audio Follower (formerly Envelope). Select a source in the left chain to edit it on the right. Sorting is for organisation: sources remain independent.\n\nDrag a source OUTPUT handle, hover over a CH tab and a module row to preview its parameters, then drop on a parameter control to create a modulation route.\n\nYou can also right-click a parameter and choose a Macro or modulation source. Drag the mapping depth strip to adjust the amount; Cmd/Ctrl-click changes polarity. Right-click the strip to remove the route. One source can drive several parameters.",
            "04 / RECOVERY, COLOURS & UPDATES\n\nShift-click a preset arrow or select a preset while holding Shift to switch without a save prompt. The previous complete edit state is retained in one recovery slot per instance. Each Shift switch replaces that slot. It travels with the DAW project, not exported presets. Restore it in Settings.\n\nSettings also provides named HEX colour profiles, default reset, language, UI scale and this guide.\n\nA newer GitHub release appears in the header. Download and verify it, explicitly confirm that ALL affected projects are saved, then close the audio hosts yourself. Installation waits for the host processes to exit. Cancelling or withholding confirmation prevents installation. OS administrator approval may still be required."
        };
        static const char* zh[] {
            u8"01 / 构建声音\n\nSpectral Corruptor 是音频效果器，需要从 DAW 输入音频。\n\n点击 + ADD EFFECT 浏览效果分类。在 CH 的 SIGNAL CHAIN 中选中模块，右侧显示参数。LOW / HIGH 指定频谱处理范围，STAGE MIX 调整效果混合量。GENERIC 时域效果必须放在通道链末端。\n\nCH 页签旁的小圆点可静音该通道。SOLO 试听选中的通道，POST SPLIT 使用分配给它的频率切片。",
            u8"02 / 拖动、复制与删除\n\n拖动效果链模块的手柄可调整顺序。悬停在其他 CH 页签上可先预览目标通道，再松开完成移动。拖动时按住 Cmd（macOS）或 Ctrl（Windows）可复制。\n\n删除模块：把模块拖到右侧效果详情区内明确标出的删除目标中，再松开。不是整个详情区域都能删除。拖到目标外或按 Esc 可取消。\n\n可使用 Undo / Redo 或 Cmd/Ctrl+Z 撤销。右键点击模块可执行复制、粘贴等操作。",
            u8"03 / 调制与宏控制\n\n打开 MODULATION，添加 LFO、Random 或 Audio Follower（原 Envelope）。左侧链列表管理和选中调制源，右侧编辑详细参数。排序只用于管理，各调制源仍独立工作。\n\n拖动调制源的 OUTPUT 输出手柄，悬停 CH 页签和模块行预览参数，然后放到目标参数控件上，即可建立映射。\n\n也可以右键点击参数，选择发送到 Macro 或调制源。拖动映射深度条改变调制量，Cmd/Ctrl 点击切换极性，右键点击深度条移除映射。一个源可控制多个参数。",
            u8"04 / 恢复、配色与更新\n\nShift 点击预设箭头，或按住 Shift 选择预设，即可无保存提示切换。切换前完整编辑状态保留在每个实例独立的一份恢复缓存中；下次 Shift 切换会覆盖它。缓存随 DAW 工程保存，不写入导出预设。可在设置中恢复。\n\n设置支持命名 HEX 配色方案、恢复默认颜色、语言、界面缩放，以及再次打开本指南。\n\nGitHub 有新版本时，顶部会显示提示。下载并校验后，必须明确确认所有受影响工程已保存，再自行关闭音频宿主。安装会等待宿主进程退出。取消或未确认时不会安装；系统可能仍要求管理员授权。"
        };
        body.setText (text (en[page], zh[page]), false); body.moveCaretToTop (false); repaint();
    }
    void resized() override
    {
        body.setBounds (24, 78, getWidth() - 48, getHeight() - 178);
        never.setBounds (24, getHeight() - 85, getWidth() - 48, 28);
        previous.setBounds (24, getHeight() - 48, 95, 30); next.setBounds (129, getHeight() - 48, 95, 30);
        done.setBounds (getWidth() - 124, getHeight() - 48, 100, 30);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, text ("QUICK GUIDE", u8"快速上手"), { 24, 18, getWidth() - 48, 38 }, 25, Theme::yellow(), true);
        Theme::text (g, juce::String (page + 1) + " / 4", { getWidth() - 85, 24, 60, 25 }, 13, Theme::dim());
    }
private:
    static juce::String text (const char* en, const char* zh) { return juce::String::fromUTF8 (PromptLanguage::get() == PromptLanguage::Language::chinese ? zh : en); }
    juce::TextEditor body; juce::TextButton previous, next, done; juce::ToggleButton never; int page {};
};
}
