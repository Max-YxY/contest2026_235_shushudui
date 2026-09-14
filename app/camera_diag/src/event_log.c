#include "event_log.h"

#include <stdio.h>

#include "inspection.h"

static const char *outcome_name(InspectionOutcome outcome)
{
    static const char *const names[] = {"PASS", "FAIL", "INPUT_FAULT", "SYSTEM_FAULT"};
    return outcome <= INSPECTION_OUTCOME_SYSTEM_FAULT ? names[outcome] : "UNKNOWN";
}

static const char *frame_status_name(FrameStatus status)
{
    static const char *const names[] = {"OK", "UNAVAILABLE", "CORRUPT"};
    return status <= FRAME_STATUS_CORRUPT ? names[status] : "UNKNOWN";
}

int event_record_format_json(const EventRecord *event, char *buffer, size_t buffer_length)
{
    if (event == NULL || buffer == NULL || buffer_length == 0U) return -1;
    const int written = snprintf(buffer, buffer_length,
        "{\"event_sequence\":%lu,\"sample_id\":\"%s\",\"outcome\":\"%s\","
        "\"reason\":\"%s\",\"offset_x\":%d,\"offset_y\":%d,"
        "\"offset_total_pixels\":%u,\"parameter_version\":%lu,"
        "\"alert_active\":%s,\"consecutive_timeouts\":%u,\"frame_status\":\"%s\"}",
        (unsigned long)event->event_sequence, event->sample_id, outcome_name(event->outcome),
        reason_code_name(event->reason), event->offset_x, event->offset_y,
        event->offset_total_pixels, (unsigned long)event->parameter_version,
        event->alert_active ? "true" : "false", event->consecutive_timeouts,
        frame_status_name(event->frame_status));
    return written >= 0 && (size_t)written < buffer_length ? written : -1;
}
