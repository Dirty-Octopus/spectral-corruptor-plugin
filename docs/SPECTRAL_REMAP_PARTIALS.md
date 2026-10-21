# FFT、Frequency Remap 与分频 Partial 效果

## FFT 窗与 Overlap

新增 Blackman-Harris（四项）、Gaussian（固定 sigma = 0.4）、Flat Top（五项）、Welch、Triangle（Bartlett）窗，保留 Hann、Hamming、Blackman、Rectangular。旧四选项宿主参数不改编号；新的 Window Extended 参数选择全部九种窗，Legacy 读取旧自动化。

Overlap 可输入 2–16 倍，步进 0.01。实际步长 `H = round(N / overlap)`，N 是经过 Oversample 扩大的 FFT 长度。界面显示 `N/H` 的实际倍率、`(1-H/N) × 100%` 的实际重叠率及 H；由于 H 必须是整数，实际倍率可能与输入略有差别。FFT 延迟仍由窗口长度决定，增加 Overlap 主要增加运算量和帧更新密度。

新窗或非 4 倍 Overlap 使用逐相位归一化的合成窗：`s[n] = w[n] / sum(w[k]^2, k mod H = n mod H)`，支持不能整除 N 的步长。Legacy 窗配合 4 倍 Overlap 保留旧合成增益。旧工程缺少新参数时回到 Legacy / 4 倍。更改 FFT、窗或 Overlap 会重建 STFT 状态。

## Frequency Remap

在普通频谱效果列表添加 Frequency Remap。曲线横轴是输入频率，纵轴是目标频率，均为 20 Hz 至有效处理上限的对数坐标。对角线不移动，REVERSE MAP 反转高低频位置；Amount 混合原频率与映射频率。

这是音频频谱的实际移动。放在其他效果前面，后续效果会接收移动后的频谱。SOURCE LOW/HIGH 选择输入频率；移到该输入区间外的内容不会被范围恢复逻辑删除。多个输入映到相同输出时叠加，超出有效范围的内容丢弃，直流和 Nyquist 保留。

节点、曲率和方向随预设、DAW 工程、效果复制粘贴及撤销保存。FFT 分辨率与瞬态会影响复杂素材的移频音色；任意曲线折叠不是无损变换。

### EDIT 与 FORMULA

默认 EDIT 保留上述手绘曲线。FORMULA 使用节点连线画布：从 `x` 输出插口连接到数学块的输入，再把结果接到 `y / OUTPUT`；也可选择节点后按 SET OUTPUT。拖动节点移动位置，拖动画布背景平移，右键输入插口断开连线，DELETE NODE 删除选中数学块。切换两种模式保留各自内容。

Hz 模式直接计算 `y = f(x)`，例如把 x 与常数 4 接到 Multiply，可把 200 Hz 移到 800 Hz。NORMALIZED (LOG) 使用与 EDIT 相同的对数频率位置：`x = log(f/20) / log(limit/20)`，输出 `f_out = 20 × (limit/20)^y`，0 对应 20 Hz、1 对应当前有效处理上限。单位切换会改变公式解释，不自动改写常数或参数。

ADD NODE 提供加减乘除、幂、正负号、绝对值、最小/最大、Clamp、平方根、指数/对数、三角函数、Floor/Ceil/Round、Modulo 和 Sign。ADD PARAM 创建可命名的参数节点，同时生成滑块；参数的 DETAILS 可设置名称、最小值、最大值、默认值、步进和正值对数范围。参数滑块支持与其他效果相同的 Macro、LFO、Follower、Random 映射，宿主自动化仍通过八个 Macro。每个公式最多 64 节点和 16 个自定义参数。

顶部 DETAILS 控制函数图像的 X/Y 显示范围、线性/对数坐标、网格和绘图采样数。它们只影响显示，不改变音频输入范围、运算精度或越界规则。数学错误、缺失连线或循环依赖会显示错误；无效计算保持对应输入频率，避免 NaN 进入音频。有效输出低于 20 Hz 或超过有效上限时丢弃。图像使用有限采样绘制，狭窄变化可能需要提高显示采样数或缩小显示范围。

公式在编辑时编译，音频线程只执行有界计算，不解析公式。普通线性公式共用一次系数计算；非线性和不连续公式在实际源频率上计算，未用低分辨率查表插值替代公式。复杂公式、更多频段和更高 Overlap 会增加运算量。图结构、参数、画布位置和图像设置均随工程、预设、复制粘贴与撤销保存。

## 分频专属效果

Stretcher、Stiff String、Centroid、P Grid、F Grid 位于 Add Effects 的独立 PARTIAL 分类页。添加后仍放在现有 CH 的频谱效果列表，可与其他频谱效果交错排列；GENERIC 时域阶段仍在 IFFT 后。Frequency Slices = 1 时这五种效果旁路，编辑值仍保留。

每段保持原来的 slice 身份和 CH 归属。包含启用的 Partial 效果的 CH 会让各段独立经过该 CH 的频谱处理，再相加；有状态的普通频谱效果也按段独立运行。因此相邻频段不会因移动、重叠或再移动被误认成新频段。该路径的运算和内存开销随分段数及效果数量增加。未使用这些效果的 CH 保持原来的合并频谱处理。

- **Scale**：整段所有频率加上同一个 Hz 位移，保留段内频率间距和复数频谱纹理。它不对段内频率做比例缩放。
- **Resynth**：根据该段能量生成连续的基础波形振荡器。Shape 可选 Sine、Triangle、Saw、Square，默认 Sine 保留原有正弦行为；Scale 模式下该选择变灰。移动后的锚点决定基频，新波形的谐波限制在有效频率范围内，并按能量归一化。使用当前分析窗构造波形频谱，经过同一个 IFFT/叠加流程；不会只点亮一个 FFT bin。即使 Amount = 0，Resynth 仍然会重新合成。
- **Band Centre**：初始锚点取原始对数频段的几何中心；后续 Partial 效果接续已移动的锚点。
- **Energy Centroid**：由该段当前频谱能量求重心；它会随素材变化，左右声道分别计算，因此仅右声道输入也能正确响应。Band Centre 的移动则在左右声道保持一致。
- **Reference**：Stretcher、Stiff String 与 P Grid 的频率参考，默认 110 Hz。

Resynth 生成的波形会由后续 Scale 继续移动；选择 Triangle、Saw 或 Square 后，不会因为经过另一个 Scale 而变回 Sine。后续显式 Resynth 则使用自己的 Shape 重新生成。三个非正弦波形共用按需准备的限带波表，避免按频段重复分配。形状选择与其他 Partial 参数一起保存到预设和工程。

参考频率记作 r，当前锚点记作 f。以下是本项目独立实现的音色变换，行为参考 NI Razor 的 Dissonance 概念，不承诺逐采样复现其内部算法：

| 效果 | 锚点变化 |
| --- | --- |
| Stretcher | `r + (f-r) × 2^(Stretch/12)`；+12 把相对 r 的距离放大两倍 |
| Stiff String | 按归一化弦刚度模型增加高频间距，r 保持不变 |
| Centroid | 按 Converge 百分比向 Target Hz 汇聚 |
| P Grid | 按半音间距量化到围绕 Reference 的等比频率格点 |
| F Grid | 按 Hz 间距量化到线性频率格点 |

锚点变化采用短平滑；频段碰撞相加，超出处理上限的移动内容丢弃。更密的分频、更大的 FFT、更高的 Overlap 及更多有状态频谱效果会一起增加成本，需按实际宿主工程评估。缓存按效果预算保护，超预算只旁路对应效果；见 [缓存维度与生命周期审计](SPECTRAL_CACHE_AUDIT.md)。Solo 的全频试听模式不运行 Partial，开启 Post Split 才按分频试听。

## Mapping 响应曲线

Mapping 每条路由都有独立的响应曲线，在来源输出之后、映射深度之前生效。它与 LFO 自定义波形分别保存：编辑一个目标的响应不会改变同一来源的其他目标。

双击空白加点，拖动节点改形状，拖动段中控制点改曲率，双击内部节点删除；LINEAR 恢复直线。右键节点先选择 X 或 Y，再选择 Macro 或调制来源。Remap、LFO Custom、Mapping 响应曲线使用相同交互；EQ 节点可分别映射 Frequency 和 Gain。黄色 Remap/Mapping 曲线保留基础形状，蓝线显示调制后的结果。

节点按固定标识保存，插入或删除其他点不会让映射错位。X 坐标保持节点原有先后顺序，不推移未调制的相邻节点；首尾 X 固定，Mapping 首尾 Y 固定，双极响应中点固定，避免中性位置偏移。删除或重置目标节点会清理它的映射。LFO 节点可由其他来源控制；自调制与循环依赖不允许。控制节点坐标的路由也有自己的响应曲线，但该层节点不支持继续嵌套映射。旧映射没有曲线时使用线性响应。
