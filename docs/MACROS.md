# Macro 与 Modulation

频谱图下方的八个 Macro 旋钮保留稳定的宿主参数 ID，可在 DAW 中自动化。右键参数滑块，勾选 Macro 或已添加的 LFO / Envelope Follower / Random，即可建立映射。一个参数可以同时接受多个来源，调制量相加，最后再限幅、按参数步进取整。

双击 Macro 上方的名称可直接改名，Enter 或失去焦点保存、Esc 取消，清空则恢复 M1–M8。右侧的数字按钮打开映射清单。名称随预设和工程保存，分配菜单、映射标题和范围条同步显示；宿主自动化 ID 与编号保持稳定。旧预设未包含名称时恢复默认名称。

## 参数调整与频率上限

参数滑块和 Macro 旋钮使用 Option（macOS）/ Alt（Windows）拖动微调；Command（macOS）/ Control（Windows）点击回默认值，双击也可重置。范围条上的 Command / Control 点击仍切换单极/双极，范围条和曲率手柄保留 Shift 精调。

Settings 中的 Spectral Frequency Limit 默认 22050 Hz，可选 11025 / 16000 / 22050 / 24000 / 44100 / 48000 / 96000 Hz。它限制 LOW / HIGH 的调节范围和实际频谱处理；高于上限的原信号绕过频谱效果，也不参与往下移频等频谱变换。GENERIC 链仍处理整个通道。FULL 表示当前上限以内；实际可处理频率还受采样率限制。

该设置随预设和工程保存；旧状态未包含设置时使用 22050 Hz。降低上限不会覆写原先保存的 LOW / HIGH 数值。LOW / HIGH 的 Macro、LFO 与其他调制深度按新范围计算，实时显示和音频处理使用同一上限。

## 参数上方的范围条

- 原滑块始终是可编辑的基础值（Centre）。分配调制不会改变它，也不会自动移动 Macro 旋钮。
- 新分配默认带 20% 的单极深度；基础值高于参数范围的 80% 时，默认向下调制，避免范围被上限截掉。旧工程已保存的深度（包括零）保持原值。
- 每个来源有独立的范围条：阴影显示范围，细线标记基础值，亮色标记显示该来源的实时位置。
- 按住范围条上下拖动调深度；向下越过零即可反向。Shift 拖动用于精调。
- macOS Command-click / Windows Control-click 切换单极和双极。
- 单极从基础值出发；双极在来源 50% 时回到基础值，两端朝相反方向变化。
- 右键范围条可删除这一条映射；参数右键菜单可取消某个来源，或移除全部调制。取消后保留基础值，不把当时的调制结果写回基础值。
- Macro 的 MAPS 按钮，以及 Modulation 卡片的 MAPS 按钮，可打开映射清单，编辑起点偏移、基础值和终点偏移。偏移以整个参数范围的百分比表示，允许负值。

线性参数在线性归一化域叠加，正值对数参数在对数归一化域叠加。显示、保存和 DSP 共用这套运算。旧的绝对 MIN/MAX/CENTRE 映射会迁移为相对偏移，并保留原来的单极/双极端点，包括不对称中心。

## MODULATION 页面

CH 标签栏右侧的 MODULATION 按钮切换独立页面。可按需增减 LFO、Envelope Follower 和 Random；为限制资源占用，总数上限为 128，映射总数上限为 256。来源通过固定 UID 关联，删除一个来源只删除其映射，其余来源与 Macro 不受影响。

LFO 提供 Sine、Triangle、Saw Up、Saw Down、Square、Custom；可编辑曲线最多 64 点。空白处双击加点，拖动方形节点移动，右键或双击删除内部点；首尾锚点保留。每段曲线中间有空心控制点，上下拖动它调整曲率，Shift 可精调。没有 Option/Alt 曲率手势。曲率随预设与工程保存；旧曲线未记录曲率时按直线读取。编辑波形自动切换 Custom。支持 0.01–40 Hz、宿主节拍同步、初始相位，以及播放从停止转为运行时重启。关闭 RESTART 时，同步 LFO 跟随宿主 PPQ（包含循环和跳转）；Hz LFO 自由运行。宿主暂时不提供节拍时沿用最近有效 BPM；启动时默认 120 BPM。此版本不使用 MIDI 音符触发。

Random 提供 Sample & Hold（阶梯随机）和 Smooth（连续平滑随机），支持 0.01–128 Hz、节拍同步和播放重启。Seed 决定可重复的随机序列；新建来源使用不同种子。同步模式的循环、跳转会回到对应节拍的随机值。图框显示实时输出历史；右键参数可 Send to Random。

Random 的 SMOOTH 参数是 0–2000 ms 的输出平滑时间，两个模式都适用。0 ms 保持原有行为；调高后跳变会更柔和、变化更慢。它是到达新目标约 63% 所需的时间，与 Rate / Sync 独立。参数编辑保留当前平滑状态，RESTART 在播放重新开始时一起重置。旧预设默认为 0 ms，设置随预设、工程和复制的调制源保存。

Bin Shuffle、Random Bin Death、Bin Teleport、Phase Corruption、Bin Hold、Frame Hold 的 Random Seed / Seed Rate 开关与控件已移除；两个 Hold 的 Random Rate 开关也由独立 Random 映射替代。Frame Hold 无需 Seed 参数。旧预设中已启用的对应随机设置会迁移为 Random 来源与 Seed/Rate 映射，使用新的随机序列，不保证与旧版本逐采样一致。Complex Rotation 的 Random Amount / Random Rate 是另一套幅度随机控制，继续保留。

Envelope Follower 提供 Gain、Rise、Fall，跟随立体声中较大的绝对幅度，输出限制为 0–100%。来源包括：

- 插件原始输入，位于 Input Gain 和效果之前。
- CH 1–4 处理前频段：取频率切分后的谱能量、在各通道效果与静音开关之前。此来源受 FFT 分析窗口和帧更新率影响，不能视为零延迟瞬态跟随器。
- Host Sidechain：独立的可选单声道/立体声输入总线。需在宿主中向本插件的 Sidechain 总线发送音频；未启用或无信号时包络按 Fall 回落到零，不回退到主输入。侧链不进入音频输出。

Gain 只改变包络检测灵敏度，不改变原音频增益。关闭来源时，其全部调制贡献归零（双极也不产生负偏移）。输入、CH 前置信号和外部侧链可避免直接从效果输出建立反馈环。

## 保存、自动化与处理顺序

旋钮数值、映射、调制源配置和曲线一起保存到预设和 DAW 工程；运行相位、临时包络状态不写入工程。旧工程没有来源时加载为空。

调制在不超过 64 个采样的块边界应用；频谱模块仍在 STFT 帧边界响应，GENERIC 模块在时域块中响应。不是逐采样宿主自动化，也不改变 FFT 本身的时间分辨率。LFO / Follower / Random 不向宿主动态增加参数；八个 Macro 是稳定的 DAW 自动化入口。

移动效果保持 UID 和全部映射；复制/粘贴效果使用新 UID，参数状态独立、仍由相同来源控制。跨实例粘贴会携带所需 LFO / Follower 定义（受数量限制），Macro 则使用目标实例对应编号的旋钮。

交互参考：[Serum 2 调制说明](https://xferrecords.com/manual/serum-2/docs)、[Ableton Envelope Follower](https://www.ableton.com/en/live-manual/11/max-for-live-devices/)。算法与界面为本项目实现。
