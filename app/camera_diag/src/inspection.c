#include "inspection.h"

#include <limits.h>
#include <string.h>

static bool frame_has_expected_layout(const Frame *frame)
{
    return frame != NULL && frame->status == FRAME_STATUS_OK &&
           frame->pixel_format == PIXEL_FORMAT_GRAY8 && frame->data != NULL &&
           frame->width > 0U && frame->height > 0U && frame->stride >= frame->width &&
           frame->data_length >= (size_t)frame->stride * frame->height;
}

static Roi effective_roi(const Frame *frame, const Roi *requested)
{
    Roi roi = *requested;
    if (roi.width == 0U || roi.height == 0U) {
        roi.x = 0U;
        roi.y = 0U;
        roi.width = frame->width;
        roi.height = frame->height;
    }
    return roi;
}

static bool roi_is_in_frame(const Frame *frame, const Roi *roi)
{
    return roi->x < frame->width && roi->y < frame->height &&
           (uint32_t)roi->x + roi->width <= frame->width &&
           (uint32_t)roi->y + roi->height <= frame->height;
}

static uint16_t absolute_i16(int16_t value)
{
    return (uint16_t)(value < 0 ? -(int32_t)value : value);
}

InspectionParameters inspection_default_parameters(void)
{
    InspectionParameters parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.foreground_threshold = 128U;
    parameters.foreground_is_bright = true;
    parameters.minimum_frame_mean = 1U;
    parameters.maximum_frame_mean = 254U;
    parameters.minimum_area_pixels = 16U;
    parameters.maximum_area_pixels = 1000000U;
    parameters.minimum_aspect_per_mille = 500U;
    parameters.maximum_aspect_per_mille = 2000U;
    parameters.maximum_offset_pixels = 5U;
    parameters.minimum_coverage_per_mille = 850U;
    parameters.version = 1U;
    return parameters;
}

InspectionResult inspection_run(const Frame *frame, const InspectionParameters *parameters)
{
    InspectionResult result;
    uint64_t intensity_sum = 0U;
    uint64_t x_sum = 0U;
    uint64_t y_sum = 0U;
    uint16_t min_x = UINT16_MAX;
    uint16_t min_y = UINT16_MAX;
    uint16_t max_x = 0U;
    uint16_t max_y = 0U;
    uint16_t x;
    uint16_t y;
    Roi roi;

    memset(&result, 0, sizeof(result));
    if (frame == NULL || parameters == NULL) {
        result.reason = REASON_SYSTEM_FAULT;
        return result;
    }
    result.reason = REASON_INPUT_INVALID;
    if (!frame_has_expected_layout(frame)) {
        result.reason = frame->status == FRAME_STATUS_UNAVAILABLE ? REASON_INPUT_TIMEOUT
                                                                  : REASON_INPUT_INVALID;
        return result;
    }

    roi = effective_roi(frame, &parameters->roi);
    if (!roi_is_in_frame(frame, &roi)) {
        result.reason = REASON_INPUT_INVALID;
        return result;
    }
    result.parameter_version = parameters->version;

    for (y = roi.y; y < (uint16_t)(roi.y + roi.height); ++y) {
        for (x = roi.x; x < (uint16_t)(roi.x + roi.width); ++x) {
            const uint8_t pixel = frame->data[(size_t)y * frame->stride + x];
            const bool foreground = parameters->foreground_is_bright
                                        ? pixel >= parameters->foreground_threshold
                                        : pixel <= parameters->foreground_threshold;
            intensity_sum += pixel;
            if (foreground) {
                ++result.foreground_area_pixels;
                x_sum += x;
                y_sum += y;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    const uint32_t pixel_count = (uint32_t)roi.width * roi.height;
    const uint8_t mean = (uint8_t)(intensity_sum / pixel_count);
    if (mean < parameters->minimum_frame_mean || mean > parameters->maximum_frame_mean) {
        return result;
    }
    result.input_valid = true;
    result.label_present = result.foreground_area_pixels > 0U;
    if (!result.label_present) {
        result.reason = REASON_LABEL_MISSING;
        return result;
    }

    result.label_box.x = min_x;
    result.label_box.y = min_y;
    result.label_box.width = (uint16_t)(max_x - min_x + 1U);
    result.label_box.height = (uint16_t)(max_y - min_y + 1U);
    result.area_valid = result.foreground_area_pixels >= parameters->minimum_area_pixels &&
                        result.foreground_area_pixels <= parameters->maximum_area_pixels;
    result.aspect_per_mille = (uint16_t)(((uint32_t)result.label_box.width * 1000U) /
                                          result.label_box.height);
    result.aspect_valid = result.aspect_per_mille >= parameters->minimum_aspect_per_mille &&
                          result.aspect_per_mille <= parameters->maximum_aspect_per_mille;

    const int16_t center_x = (int16_t)(x_sum / result.foreground_area_pixels);
    const int16_t center_y = (int16_t)(y_sum / result.foreground_area_pixels);
    result.offset_x = (int16_t)(center_x - (int16_t)parameters->expected_center_x);
    result.offset_y = (int16_t)(center_y - (int16_t)parameters->expected_center_y);
    result.offset_total_pixels = (uint16_t)(absolute_i16(result.offset_x) + absolute_i16(result.offset_y));
    result.position_valid = result.offset_total_pixels <= parameters->maximum_offset_pixels;
    result.coverage_per_mille = (uint16_t)(((uint64_t)result.foreground_area_pixels * 1000U) /
                                           ((uint32_t)result.label_box.width * result.label_box.height));
    result.surface_valid = result.coverage_per_mille >= parameters->minimum_coverage_per_mille;

    if (!result.area_valid) result.reason = REASON_LABEL_AREA_OUT_OF_RANGE;
    else if (!result.aspect_valid) result.reason = REASON_LABEL_ASPECT_OUT_OF_RANGE;
    else if (!result.position_valid) result.reason = REASON_LABEL_POSITION_OUT_OF_RANGE;
    else if (!result.surface_valid) result.reason = REASON_LABEL_SURFACE_ANOMALY;
    else result.reason = REASON_OK;
    return result;
}

const char *reason_code_name(ReasonCode reason)
{
    static const char *const names[] = {
        "OK", "INPUT_INVALID", "INPUT_TIMEOUT", "LABEL_MISSING", "LABEL_AREA_OUT_OF_RANGE",
        "LABEL_ASPECT_OUT_OF_RANGE", "LABEL_POSITION_OUT_OF_RANGE", "LABEL_SURFACE_ANOMALY",
        "SYSTEM_FAULT", "AI_UNAVAILABLE"
    };
    return reason <= REASON_AI_UNAVAILABLE ? names[reason] : "UNKNOWN";
}
