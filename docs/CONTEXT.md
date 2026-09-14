# Project Progress Entry Point

Before making a project change, reporting progress, or reconstructing context,
read and update `docs/project-progress-ledger.md`. It is the single source of
truth for every module's status, percentage, evidence, dependency, and next
acceptance condition.

# 标签质检领域词汇

| 术语 | 定义 |
|---|---|
| 标签 | 被检的生产标签本体，不等同于图像文件或样本编号。 |
| 样本 | 一次可追溯的输入实例，可来自离线文件或未来的采集适配器。 |
| 帧（`Frame`） | 算法处理的单幅像素输入及其输入状态。 |
| 帧状态（`FrameStatus`） | 帧的来源状态：`OK` 正常、`UNAVAILABLE` 采集超时类、`CORRUPT` 格式/损坏类。 |
| 输入超时（`INPUT_TIMEOUT`） | 采集适配层未在约定时间内提供有效帧时产生的输入故障原因码。 |
| 输入无效（`INPUT_INVALID`） | 帧布局、ROI 或亮度不变量违反规则输入前提时的输入故障原因码。 |
| 检测（Inspection） | 基于参数对一帧标签进行规则判断的过程，不负责报警。 |
| 检测结果（`InspectionResult`） | 检测产生的几何测量、规则布尔值和主原因码。 |
| 判定（Decision） | 将检测结果和运行状态转换为业务结果、计数和报警请求的过程。 |
| 结果（Outcome） | 一次判定的业务分类：合格、不合格、输入故障或系统故障。 |
| 原因码（Reason Code） | 对一次结果最优先、机器可读的解释；它不是底层驱动错误文本。 |
| 告警（Alert） | 判定层提出的输出请求；是否驱动物理 GPIO 由适配器决定。 |
| 事件（`EventRecord`） | 可追加、可序列化的一次判定记录。 |
| 参数版本 | 唯一标识一组检测阈值和 ROI 的版本号，用于重现检测结果。 |
| AI 评估 | 可选的独立评估，不得替代规则检测或改变规则结果。 |
| 采集批次 | 在同一工装、补光、相机/离线输入设置下取得的一组样本；它不是数据集版本。 |
| 工装 | 用于固定标签位置、距离、姿态和背景的物理装置。 |
| 真值 | 经双人复核后，对样本实际状态和预期主原因码作出的可追溯标注。 |
| 标定集 | 只用于确定 ROI 与规则参数的样本集合；不得用于最终效果报告。 |
| 独立验证集 | 从未参与 ROI、阈值或参数选择的样本集合，用于后续客观评估。 |
| 输入无效样本 | 用于验证采集/解析故障处理的非产品缺陷样本；不进入标签缺陷的标定范围。 |
