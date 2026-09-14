# 质检接口契约（冻结版，对话 05）

**状态**：冻结于 2026-08-09。公开 C 接口位于 `include/`，主机测试与 openvela 板端共享同一核心。
**生效范围**：规则质检主线。不得将 GPIO/LED、定时器、中断、相机、显示、存储、网络或 AI 当作已验证前提。

## 1. 责任边界与禁止依赖

| 模块 | 输入 | 输出 | 禁止职责 |
|---|---|---|---|
| 采集适配器 | 离线 PGM 或未来相机数据 | `Frame` 或明确输入故障 | 不做规则判定、告警或原因优先级 |
| 规则检测（`inspection_run`） | `Frame`、`InspectionParameters` | `InspectionResult` | 不维护事件计数、不驱动硬件、不调用 AI |
| 判定状态机（`decision_update`） | `InspectionResult`、`DecisionState` | `DecisionResult` | 不读取像素、不格式化传输协议 |
| 事件适配器（`event_record_*`） | `Frame`、检测结果、判定结果 | `EventRecord` / JSON Lines | 不改变判定或原因码 |
| 告警适配器 | `DecisionResult.alert_active` | 串口记录或主机 GPIO mock | 不自行去抖、不覆盖规则结论 |
| AI 评估适配器 | 同一受控样本 | 独立 `AiAssessment` 状态 | 不替代规则结果、不得在 AI 不可用时制造产品 FAIL |

- 规则核心不调用任何硬件 SDK；`alert_active` 只来自判定层；AI 不修改规则结果。
- 板端未验证能力（GPIO/LED、定时器、中断、相机、显示、文件系统、网络、AI）不得成为任何接口的必需前提。

## 2. `Frame` 契约（冻结）

```c
typedef struct {
    char sample_id[SAMPLE_ID_MAX_LENGTH + 1U]; /* 见下 */
    PixelFormat pixel_format;                  /* 仅 PIXEL_FORMAT_GRAY8 */
    const uint8_t *data;                       /* 采集方所有，见所有权 */
    uint16_t width;
    uint16_t height;
    uint16_t stride;                           /* 必须 >= width */
    size_t data_length;                        /* 必须 >= stride * height */
    FrameStatus status;                        /* OK / UNAVAILABLE / CORRUPT */
} Frame;
```

- **布局**：GRAY8；`stride >= width`；`data_length >= stride*height`；非法布局由 `inspection_run` 判为输入故障，绝不未定义行为。
- **所有权**：`data` 指向采集适配器持有的缓冲区；仅在 `inspection_run` 同步调用期间有效；规则核心不持有、不释放、不修改。
- **`sample_id`**：`SAMPLE_ID_MAX_LENGTH = 31`（不含终止符）；内容限定 ASCII 可打印字符（0x20–0x7E）且**不含 `"` 与 `\`**（保证 JSON 单行安全）；由生产者保证，事件格式化不做转义。
- **采集时间/来源**：本版不包含；未来相机元数据通过独立扩展结构携带，不硬塞入 `Frame`。
- **状态语义**：`OK` 正常；`UNAVAILABLE` 采集超时类；`CORRUPT` 格式/损坏类。`FrameStatus` 与原因码映射见第 4 节。

## 3. 参数契约（摘要，详版见 `docs/inspection-parameter-format.md`）

- 无 section 的 UTF-8 ASCII `key=value` 文本；`#` 注释与空行跳过。
- **唯一版本键** `parameter_version`：非零递增整数，**必填**（缺省或为 0 加载失败）。
- 未给出的键使用 `inspection_default_parameters()` 默认值（覆盖默认值模式）。
- **重复键、未知键、空值、非法布尔、数值溢出、范围反转（min>max）、`minimum_coverage_per_mille > 1000` 均加载失败**。
- ROI `0x0` 表示全帧；ROI 越界在运行期（`inspection_run`）判为输入故障。
- 合成样例参数与真实工装参数使用不同文件名、`parameter_version` 与说明，不得混用。
- 所有键：`parameter_version, roi_x, roi_y, roi_width, roi_height, foreground_threshold, foreground_is_bright, minimum_frame_mean, maximum_frame_mean, minimum_area_pixels, maximum_area_pixels, minimum_aspect_per_mille, maximum_aspect_per_mille, expected_center_x, expected_center_y, maximum_offset_pixels, minimum_coverage_per_mille`。

## 4. 原因码与优先级（冻结）

| 分类 | 原因码 | 产生方 | 判定映射 | 报警 |
|---|---|---|---|---|
| 正常 | `OK` | 规则层（全部规则通过且输入有效） | `PASS` | 否 |
| 输入故障 | `INPUT_INVALID` | 规则层（布局/ROI/亮度不变量违反；`FRAME_STATUS_CORRUPT` 类） | `INPUT_FAULT` | 去抖后 |
| 输入故障 | `INPUT_TIMEOUT` | 规则层（`FRAME_STATUS_UNAVAILABLE`） | `INPUT_FAULT` | 去抖后 |
| 产品不合格 | `LABEL_MISSING` | 规则层 | `FAIL` | 立即 |
| 产品不合格 | `LABEL_AREA_OUT_OF_RANGE` | 规则层 | `FAIL` | 立即 |
| 产品不合格 | `LABEL_ASPECT_OUT_OF_RANGE` | 规则层 | `FAIL` | 立即 |
| 产品不合格 | `LABEL_POSITION_OUT_OF_RANGE` | 规则层 | `FAIL` | 立即 |
| 产品不合格 | `LABEL_SURFACE_ANOMALY` | 规则层 | `FAIL` | 立即 |
| 系统故障 | `SYSTEM_FAULT` | 判定层/规则层（空指针、状态损坏、不变量破坏） | `SYSTEM_FAULT` | 立即 |
| AI 状态 | `AI_UNAVAILABLE` | 仅独立 AI 评估适配层（诊断） | 不改变规则结果 | 不适用 |

- **产品原因优先级（单一主原因码）**：`LABEL_MISSING` > `LABEL_AREA_OUT_OF_RANGE` > `LABEL_ASPECT_OUT_OF_RANGE` > `LABEL_POSITION_OUT_OF_RANGE` > `LABEL_SURFACE_ANOMALY`；多项失败时 `InspectionResult.reason` 取最高优先级，其余测量字段全部保留。
- **系统故障优先**：`SYSTEM_FAULT` 高于任何产品不合格原因。
- `INPUT_INVALID` 与 `INPUT_TIMEOUT` 是两个独立原因码，均映射 `INSPECTION_OUTCOME_INPUT_FAULT`，共用同一去抖计数（`consecutive_timeouts`）。
- `AI_UNAVAILABLE` 只作独立 AI 评估状态或诊断；不得改变 `InspectionResult` 的规则原因或 `DecisionResult.outcome`。

## 5. 判定状态机（冻结）

状态：`DecisionState{ next_event_sequence, pass_count, fail_count, input_fault_count, consecutive_timeouts, timeout_alert_threshold }`。

| 输入 `InspectionResult.reason` | `outcome` | 计数 | `consecutive_timeouts` | `alert_active` |
|---|---|---|---|---|
| `OK` | `PASS` | `pass_count++` | 重置 0 | false |
| `INPUT_INVALID` / `INPUT_TIMEOUT` | `INPUT_FAULT` | `input_fault_count++` | 递增（上限 `UINT8_MAX`） | `consecutive_timeouts >= timeout_alert_threshold` |
| 产品原因（`LABEL_*`） | `FAIL` | `fail_count++` | 重置 0 | **true（立即）** |
| `SYSTEM_FAULT` | `SYSTEM_FAULT` | 不计入上述计数 | 保持不变 | **true（立即）** |

- `decision_init(state, threshold)`：threshold 为 0 时按 1 处理；`next_event_sequence` 从 1 开始。
- `state == NULL || inspection == NULL` → 返回 `SYSTEM_FAULT` 且 `alert_active=true`。
- 溢出：`next_event_sequence` 为 `uint32_t` 自然回绕；`consecutive_timeouts` 在 `UINT8_MAX` 饱和。
- `alert_active` **只来自判定层**；输入故障不被误算为产品不合格。

## 6. `EventRecord` 与 JSON Lines（冻结）

字段（必填加粗；诊断字段事件必带，因结构固定）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `event_sequence` | uint32 | 判定层序号 |
| `sample_id` | 字符串 | 见第 2 节约束 |
| `outcome` | 枚举文本 | `PASS`/`FAIL`/`INPUT_FAULT`/`SYSTEM_FAULT` |
| `reason` | 枚举文本 | 见第 4 节 |
| `offset_x` / `offset_y` / `offset_total_pixels` | 整数 | 规则测量（诊断） |
| `parameter_version` | uint32 | 参数版本 |
| `alert_active` | bool | 判定层报警请求 |
| `consecutive_timeouts` | uint8 | 连续输入故障计数 |
| `frame_status` | 枚举文本 | `OK`/`UNAVAILABLE`/`CORRUPT`（诊断，记录原始输入状态） |

单行 JSON（无内嵌换行，结尾 `\n` 由适配器追加）：

```json
{"event_sequence":1,"sample_id":"pass","outcome":"PASS","reason":"OK","offset_x":0,"offset_y":0,"offset_total_pixels":0,"parameter_version":1,"alert_active":false,"consecutive_timeouts":0,"frame_status":"OK"}
```

- 缓冲区不足或空参数返回 -1；写入内容保证 `NUL` 终止。
- 枚举文本与代码/测试使用同一文本；`sample_id` 由生产者保证 JSON 安全（见第 2 节）。
- 事件可由串口或文件适配器逐行追加，不依赖网络/文件系统/GPIO。

## 7. 不变量与扩展策略

- 公开 C 结构变更必须先更新本契约、测试与迁移说明（见 `docs/conversation-05-conclusion.md` 迁移记录）。
- 相机元数据、AI 评估等扩展通过独立结构/适配层引入，不修改规则核心语义。
- 合成样例与参数（`data/samples/*.pgm`、`config/synthetic-sample-parameters.ini`）仅用于代码通路，不得用于真实标定或效果结论。
