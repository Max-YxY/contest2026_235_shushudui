#ifndef LABEL_INSPECTION_TYPES_H
#define LABEL_INSPECTION_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAMPLE_ID_MAX_LENGTH 31U

typedef enum {
    PIXEL_FORMAT_GRAY8 = 0,
} PixelFormat;

typedef enum {
    FRAME_STATUS_OK = 0,
    FRAME_STATUS_UNAVAILABLE,
    FRAME_STATUS_CORRUPT,
} FrameStatus;

typedef struct {
    char sample_id[SAMPLE_ID_MAX_LENGTH + 1U]; /* ASCII 可打印(0x20-0x7E)，不含 '"' 与 '\\'；见 interface-contract.md */
    PixelFormat pixel_format;                  /* 仅 PIXEL_FORMAT_GRAY8 */
    const uint8_t *data;                       /* 采集方所有，仅同步调用期有效；规则层不持有/不释放 */
    uint16_t width;
    uint16_t height;
    uint16_t stride;                           /* 必须 >= width */
    size_t data_length;                        /* 必须 >= stride * height */
    FrameStatus status;                        /* OK / UNAVAILABLE(超时类) / CORRUPT(损坏类) */
} Frame;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} Roi;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} BoundingBox;

typedef enum {
    REASON_OK = 0,
    REASON_INPUT_INVALID,
    REASON_INPUT_TIMEOUT,
    REASON_LABEL_MISSING,
    REASON_LABEL_AREA_OUT_OF_RANGE,
    REASON_LABEL_ASPECT_OUT_OF_RANGE,
    REASON_LABEL_POSITION_OUT_OF_RANGE,
    REASON_LABEL_SURFACE_ANOMALY,
    REASON_SYSTEM_FAULT,
    REASON_AI_UNAVAILABLE,
} ReasonCode;

typedef struct {
    Roi roi;
    uint8_t foreground_threshold;
    bool foreground_is_bright;
    uint8_t minimum_frame_mean;
    uint8_t maximum_frame_mean;
    uint32_t minimum_area_pixels;
    uint32_t maximum_area_pixels;
    uint16_t minimum_aspect_per_mille;
    uint16_t maximum_aspect_per_mille;
    uint16_t expected_center_x;
    uint16_t expected_center_y;
    uint16_t maximum_offset_pixels;
    uint16_t minimum_coverage_per_mille;
    uint32_t version;
} InspectionParameters;

typedef struct {
    bool input_valid;
    bool label_present;
    bool area_valid;
    bool aspect_valid;
    bool position_valid;
    bool surface_valid;
    BoundingBox label_box;
    uint32_t foreground_area_pixels;
    uint16_t aspect_per_mille;
    int16_t offset_x;
    int16_t offset_y;
    uint16_t offset_total_pixels;
    uint16_t coverage_per_mille;
    uint32_t parameter_version;
    ReasonCode reason;
} InspectionResult;

typedef enum {
    INSPECTION_OUTCOME_PASS = 0,
    INSPECTION_OUTCOME_FAIL,
    INSPECTION_OUTCOME_INPUT_FAULT,
    INSPECTION_OUTCOME_SYSTEM_FAULT,
} InspectionOutcome;

typedef struct {
    uint32_t event_sequence;
    InspectionOutcome outcome;
    ReasonCode reason;
    bool alert_active;
    uint8_t consecutive_timeouts;
    uint32_t pass_count;
    uint32_t fail_count;
    uint32_t input_fault_count;
} DecisionResult;

typedef struct {
    uint32_t next_event_sequence;
    uint32_t pass_count;
    uint32_t fail_count;
    uint32_t input_fault_count;
    uint8_t consecutive_timeouts;
    uint8_t timeout_alert_threshold;
} DecisionState;

typedef struct {
    uint32_t event_sequence;
    char sample_id[SAMPLE_ID_MAX_LENGTH + 1U];
    InspectionOutcome outcome;
    ReasonCode reason;
    int16_t offset_x;
    int16_t offset_y;
    uint16_t offset_total_pixels;
    uint32_t parameter_version;
    bool alert_active;          /* 判定层报警请求（冻结必填） */
    uint8_t consecutive_timeouts; /* 连续输入故障计数（冻结必填） */
    FrameStatus frame_status;   /* 原始输入状态诊断（OK/UNAVAILABLE/CORRUPT） */
} EventRecord;

#endif
