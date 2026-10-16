# Spectral Corruptor v0.2.0 - UX reference / 交互说明

This document describes the implemented interaction contract. It does not replace
host-specific listening, installation or compatibility validation. Full bilingual
manuals are generated from current source declarations by `docs/manual/`.

## 中文

### 预设与单份恢复

普通预设切换保留未保存提示。Shift 点击前后箭头，或按住 Shift 选择预设，
直接切换且不保存预设；切换前的完整编辑状态进入当前实例的一份缓存。
下一次成功的 Shift 切换覆盖它。缓存随 DAW 工程保存，不进入 `.scpreset`。
设置中可恢复；恢复是一项可 Undo 的编辑。它不是多版本历史或自动保存。

### 效果链与删除

- 拖动模块手柄排序；悬停 CH 页签先预览，再放到插入位置完成跨 CH 移动。
- 拖动时按 Cmd（macOS）或 Ctrl（Windows）复制；右键模块也有复制／粘贴。
- 拖到右侧效果详情内明确标出的删除目标后松开删除。整个详情页不是删除区。
- Esc 或松开到无效区域取消；完成的编辑可 Undo / Redo。
- GENERIC 时域效果保持在该 CH 的频谱效果之后。

### Header 图示动画

每种效果的 Header 前景矢量图在效果启用且可见时循环，即使没有音频输入也会运动。
停用效果会冻结；隐藏或被其他页面遮住时暂停绘制。Magnitude Phase Swap 用双图形
旋转交换表达重组，Complex Rotation 使用旋转相量；相位、频率与纹理类使用轨道节点、
行进波与层片拖尾表达处理思路。

GENERIC 保留随相关参数变化的曲线并叠加运动标记；Spectral Clipper 的阈值与
Spectral Equalizer 的响应也可随参数观察。其他图形主要是原理示意，不代表所有参数
都联动。动画不是实时音频分析、输出电平或宿主节拍读数。

### MODULATION 与 Audio Follower

Envelope 更名为 Audio Follower，旧工程兼容。左侧列表管理来源，右侧编辑详情；
来源仍独立运行，列表排序不会把来源变成串行 DSP 链。

抓住来源 OUTPUT，悬停目标 CH，再悬停效果模块以预览参数，最后放到支持映射的
数字参数上。一个来源可建立多个目标映射。另一个入口是右键参数的 SEND TO
MACRO / LFO / AUDIO FOLLOWER / RANDOM。新映射深度为 20%；拖深度条调节正负深度，
Cmd/Ctrl 点击切换单极／双极，右键深度条移除。原参数是基础值，多个来源叠加后限幅。

### 新增频谱效果

**Spectral Clipper** 位于 DESTRUCTION。逐 FFT bin 削幅并保留相位，参数为 Threshold
(-96 到 0 dBFS)、Drive (0-48 dB)、Knee (0-24 dB)、Output (-48 到 +24 dB)。阈值按
实际 FFT 长度与窗的 coherent amplitude 校准；这不限制重建波形的最终峰值。

**Spectral Equalizer** 位于 FILTER / UTILITY。八段提供 On、Shape、Frequency、Gain、Q；
默认频率 40/100/250/630/1600/4000/10000/16000 Hz，全部是启用的 0 dB Bell。
形状包括 Bell、Low Shelf、High Shelf、Low Cut、High Cut。保留输入 bin 相位，
沿用现有 FFT 延迟。拖节点改变频率／增益，Alt/Option 精调，滚轮调 Q，双击复位
该段 Frequency/Gain/Q；右键选形状／启停。Cut 不使用 Gain。总提升封顶 +96 dB。

### 本机偏好与引导

配色有 Background、Panel、Field、Border、Text、Muted text、Inactive、Red accent、
Yellow accent、Blue accent 十槽。输入六位 RGB HEX（可带 #），按名称保存多个方案；
Default 保留为默认，随时重置。方案保存在当前用户本机 UI 偏好中并供实例共享，
不随声音预设或工程传输。UI SCALE 25%-200%，25% 一档。

激活后打开编辑器显示 Quick Guide；确认只关闭本次，勾选“以后不再显示”并确认才
停用自动显示。设置可随时重开。指南跟随中英提示语言，覆盖效果拖动／删除和调制。

### 安装与更新

Windows 安装器可选 VST3、Standalone、中英手册，各组件目录可选择并记录。
macOS 可选 VST3、AU、Standalone、中英手册；检测 `~/Library`、`/Library` 和保存的
自定义目录。任意目录不保证 DAW 自动扫描；AU 与 VST3 优先使用标准插件位置。
macOS 默认勾选“沿用上次安装目录”；各组件 Location 保持默认时沿用上次路径，
改为自选 Location 则安装到该位置。要回到标准目录须取消“沿用上次安装目录”。
记录位于 `/Library/Application Support/SpectralCorruptor/Installer/choices.plist`。

GitHub 检查到新正式版本时，HEADER 提示 Newer Version Available / 新版本可用。
下载安装器并验证发行 SHA-256 后，用户必须明确确认所有受影响工程已保存，
再正常退出所有相关宿主。安装等待进程退出；取消或不确认不会安装。
更新不会自动保存工程或强制关闭宿主。安装后重开 DAW 并核对实际加载版本。

## English

### Presets and recovery

Ordinary switching retains the unsaved-change prompt. Shift-click an arrow, or
hold Shift while selecting a preset, to switch without prompting or writing a
preset file. The complete previous edit state enters one slot per instance; the
next successful Shift switch replaces it. Save the DAW project to persist the
slot. `.scpreset` excludes it. Restore in Settings; restoration supports Undo.

### Effects and modulation

Drag a stage handle to reorder. Hover another CH tab to preview, then release at
the insertion marker to move. Cmd/Ctrl-drag copies. Only the explicitly marked
removal target in the detail area deletes a module; Esc or an invalid drop cancels.
GENERIC stages remain after spectral stages. The stage context menu provides copy/paste.

Every effect header has a foreground vector illustration that loops while the
effect is enabled and visible, including with no audio input. Disabling freezes
it; hidden or covered pages stop drawing. Magnitude Phase Swap rotates and
exchanges two shapes; Complex Rotation uses a rotating phasor. Phase, frequency
and texture effects use orbiting nodes, travelling waves and layered trails.
GENERIC retains its parameter curves with moving markers; relevant edits reshape
them. Spectral Clipper shows its threshold and Spectral Equalizer its response.
Other illustrations mainly express the concept; not every parameter affects them.
These are not live audio analysis, output meters or host-beat indicators.

Audio Follower replaces the Envelope display name without breaking old state.
The left source list and right inspector share the CH organisational model, but
sources remain independent. Drag OUTPUT, hover CH and stage to preview, then drop
onto a supported numeric parameter. Alternatively, right-click the target and use
SEND TO MACRO / LFO / AUDIO FOLLOWER / RANDOM. New routes start at 20% depth. Drag
the mapping strip for signed depth, Cmd/Ctrl-click for polarity, or right-click
the strip to remove. The parameter remains the base; source offsets sum and clamp.

### New spectral effects

Spectral Clipper preserves phase while limiting per-bin magnitude. Threshold,
Drive, Knee and Output are calibrated using actual FFT/window amplitude. The
ceiling is not a waveform or true-peak limiter. Spectral Equalizer has eight
phase-preserving bands with Bell, Low Shelf, High Shelf, Low Cut and High Cut.
Drag nodes for frequency/gain, Alt/Option-drag for fine adjustment, scroll for Q,
double-click to reset Frequency/Gain/Q, and right-click for shape/enable. Cuts
ignore Gain. Default Bell gains are neutral; combined boost is capped at +96 dB.

### Preferences, guide and update handoff

Ten six-digit RGB HEX slots form named colour profiles. Profiles, UI scale and
guide visibility are local user preferences, shared by instances rather than
included in presets/projects. Reset restores the original palette. Quick Guide
appears after activation when the editor opens until Do not show again is checked
and confirmed; Settings can reopen it. Both prompt languages are available.

Windows offers VST3, Standalone and bilingual manuals, with remembered component
paths. macOS adds AU and checks per-user/system Library locations plus saved
custom paths. Hosts may not scan arbitrary plugin folders.
On macOS, Keep previous installation locations is selected by default. Unchanged
Location defaults reuse the remembered folder; a custom Location takes precedence.
Deselect the option to return to standard folders. Choices are recorded in
`/Library/Application Support/SpectralCorruptor/Installer/choices.plist`.

The GitHub header badge opens an update flow: download, verify the release SHA-256,
explicitly confirm every affected project is saved, then quit audio hosts normally.
Installation waits for host processes to exit. Cancelling or withholding consent
prevents installation. The plugin never saves projects or kills host processes.
After installation, reopen the host and verify the version it actually loaded.

## Source and generation

- Parameter inventory: `Source/modules/ModuleSpecs.h`, `DelayTiming.h`,
  `SpectralEqualizer.h`; parsed by `docs/manual/manual_parameters.py`.
- Full manuals: `build_manual.py`, `build_manual_en.py`, and shared
  `manual_v02.py`; output copies in `docs/manual/` and `output/pdf/`.
- DSP: `SpectralClipper.h`, `SpectralEqualizer.h`, `FFTEngine`, `SpectralProcessor`.
- Interactions: `EditorContent`, `ModulationPage`, `AssignableSlider`, `PresetBar`,
  `ThemeProfiles`, `QuickGuide`, and `Source/updates/`.
- EQ prototype magnitude equations: [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/).
