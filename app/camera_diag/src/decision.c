#include "decision.h"

#include <string.h>

void decision_init(DecisionState *state, uint8_t timeout_alert_threshold)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->next_event_sequence = 1U;
    state->timeout_alert_threshold = timeout_alert_threshold == 0U ? 1U : timeout_alert_threshold;
}

DecisionResult decision_update(DecisionState *state, const InspectionResult *inspection)
{
    DecisionResult decision;
    memset(&decision, 0, sizeof(decision));
    if (state == NULL || inspection == NULL) {
        decision.outcome = INSPECTION_OUTCOME_SYSTEM_FAULT;
        decision.reason = REASON_SYSTEM_FAULT;
        decision.alert_active = true;
        return decision;
    }

    decision.event_sequence = state->next_event_sequence++;
    decision.reason = inspection->reason;
    if (inspection->reason == REASON_OK) {
        decision.outcome = INSPECTION_OUTCOME_PASS;
        ++state->pass_count;
        state->consecutive_timeouts = 0U;
    } else if (inspection->reason == REASON_INPUT_INVALID ||
               inspection->reason == REASON_INPUT_TIMEOUT) {
        decision.outcome = INSPECTION_OUTCOME_INPUT_FAULT;
        ++state->input_fault_count;
        if (state->consecutive_timeouts < UINT8_MAX) ++state->consecutive_timeouts;
        decision.alert_active = state->consecutive_timeouts >= state->timeout_alert_threshold;
    } else if (inspection->reason == REASON_SYSTEM_FAULT) {
        decision.outcome = INSPECTION_OUTCOME_SYSTEM_FAULT;
        decision.alert_active = true;
        /* SYSTEM_FAULT 不计入 pass/fail/input_fault，也不重置或递增 timeouts */
    } else {
        decision.outcome = INSPECTION_OUTCOME_FAIL;
        ++state->fail_count;
        state->consecutive_timeouts = 0U;
        decision.alert_active = true;
    }
    decision.pass_count = state->pass_count;
    decision.fail_count = state->fail_count;
    decision.input_fault_count = state->input_fault_count;
    decision.consecutive_timeouts = state->consecutive_timeouts;
    return decision;
}

EventRecord event_record_make(const Frame *frame, const InspectionResult *inspection,
                              const DecisionResult *decision)
{
    EventRecord event;
    memset(&event, 0, sizeof(event));
    if (frame != NULL) {
        memcpy(event.sample_id, frame->sample_id, SAMPLE_ID_MAX_LENGTH);
        event.frame_status = frame->status;
    }
    if (inspection != NULL) {
        event.offset_x = inspection->offset_x;
        event.offset_y = inspection->offset_y;
        event.offset_total_pixels = inspection->offset_total_pixels;
        event.parameter_version = inspection->parameter_version;
    }
    if (decision != NULL) {
        event.event_sequence = decision->event_sequence;
        event.outcome = decision->outcome;
        event.reason = decision->reason;
        event.alert_active = decision->alert_active;
        event.consecutive_timeouts = decision->consecutive_timeouts;
    }
    return event;
}
