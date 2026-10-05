# GENERIC 时域效果

每个 CH 的处理顺序固定为：频段切分 → 频谱效果 → IFFT → GENERIC → 通道汇总 → 降采样 → Master Dry/Wet / Output Gain。

一次频谱分析供四条通道分别重建音频；每个通道有自己的混响、延迟、压缩、失真状态。GENERIC 始终排列在频谱效果之后。试图拖到 FFT 前面，或把 FFT 拖到 GENERIC 后面时，会提示 `GENERIC EFFECTS MUST STAY AT THE END OF THE CHAIN`，取消这次无效移动并保留原有顺序；提示在松手后短暂保留。Utility 也在所属 CH 的时域链中，不再依次作用于已经汇总的所有通道。

- Reverb：Plate / Hall / Room 三组不同长度的八路反馈延迟网络，提供 Decay、Size、Pre-delay、Damping、Mix。Plate 是算法式 plate 风格，不是实测脉冲或硬件仿真。
- Delay：普通双声道、三抽头 Tap Delay、左右交替 Ping-Pong。Time 1–2000 ms，Feedback 最大 95%，Mix 可调。时间变化经过平滑。
- Compressor：立体声联动的峰值检测，Threshold、Ratio、Attack、Release、Knee、Makeup、Mix。
- Distortion：Tube、Overdrive、Sin Fold、Lin Fold，加 Drive、Bias、Tone、Output、Mix。Tube 有非对称曲线，输出做 DC 抑制；可使用全局过采样降低高频折叠。

CH 左边圆点可点击：已分配频段且启用的 CH 以固定 1 Hz 闪绿灯（亮 250 ms、暗 750 ms，硬切，无呼吸渐变），与音频和宿主播放状态无关。点击静音后常亮红灯；未分配频段的 CH 保持灰色，不闪烁。只有一个 Frequency Slice 时，CH 1 仍然闪绿灯，CH 2–4 灰色。灯没有黑色外框。静音使该 CH 的处理输出（含尾音）归零，并有约 5 ms 的增益过渡。Master Dry/Wet 的干声支路和宿主 Bypass 保留其原有全局作用。

将效果拖到目标 CH 标签即可移动；Command（macOS）/ Control（Windows）拖拽为复制。右键效果 Copy，右键链空白 Paste。拖到右侧中心移除；复制手势不会触发移除。

FFT Size 新入口支持 128、256、512、1024、2048、4096、8192、16384、32768。标有 `/ L` 的 Legacy 选项显示实际尺寸并读取原四档 FFT Size 自动化，防止旧工程归一化参数改变含义。过采样维持原有频率分辨率，32768 × 4 的内部变换大小为 131072。大 FFT 增加延迟、CPU 和历史类效果的内存用量。

## Parameter-driven artwork

The four GENERIC headers update at the editor's 30 Hz cadence, including effective Macro/LFO/Follower/Random values. Compressor plots its soft-knee transfer plus attack/release detector response. Distortion uses the same waveshaping function as the audio processor, including drive/bias/output/mix; the lower tone strip denotes its subsequent filter. The static plot is before the tone/DC filters. Reverb's space geometry follows mode/size and its decay envelope follows RT60. Delay's 6-second timeline follows time, feedback and normal/tap/ping-pong topology. These are parameter visualizations, not output oscilloscopes. Other effect artwork is unchanged.

CH 绿灯每四分音符闪烁一次，播放时对齐宿主 PPQ，停止时按最近的宿主 BPM 继续闪烁；所有 CH 共用相位。静音时常亮红色，没有分配频段时常亮灰色。
