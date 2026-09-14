#ifndef LABEL_INSPECTION_H
#define LABEL_INSPECTION_H

#include "inspection_types.h"

InspectionParameters inspection_default_parameters(void);
InspectionResult inspection_run(const Frame *frame, const InspectionParameters *parameters);
const char *reason_code_name(ReasonCode reason);

#endif
