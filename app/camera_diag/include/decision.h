#ifndef LABEL_INSPECTION_DECISION_H
#define LABEL_INSPECTION_DECISION_H

#include "inspection_types.h"

void decision_init(DecisionState *state, uint8_t timeout_alert_threshold);
DecisionResult decision_update(DecisionState *state, const InspectionResult *inspection);
EventRecord event_record_make(const Frame *frame, const InspectionResult *inspection,
                              const DecisionResult *decision);

#endif
