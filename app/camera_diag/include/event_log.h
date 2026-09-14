#ifndef LABEL_INSPECTION_EVENT_LOG_H
#define LABEL_INSPECTION_EVENT_LOG_H

#include <stddef.h>

#include "inspection_types.h"

int event_record_format_json(const EventRecord *event, char *buffer, size_t buffer_length);

#endif
