#include "parameter_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inspection.h"

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int parse_u32(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' || parsed > maximum) return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int assign_u16(const char *value, uint16_t *field)
{
    uint32_t parsed;
    if (parse_u32(value, UINT16_MAX, &parsed) != 0) return -1;
    *field = (uint16_t)parsed;
    return 0;
}

static int assign_u8(const char *value, uint8_t *field)
{
    uint32_t parsed;
    if (parse_u32(value, UINT8_MAX, &parsed) != 0) return -1;
    *field = (uint8_t)parsed;
    return 0;
}

static FILE *open_read_file(const char *path)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, path, "r") == 0 ? file : NULL;
#else
    return fopen(path, "r");
#endif
}

static int set_parameter(InspectionParameters *parameters, const char *key, const char *value)
{
    if (strcmp(key, "parameter_version") == 0) return parse_u32(value, UINT32_MAX, &parameters->version);
    if (strcmp(key, "roi_x") == 0) return assign_u16(value, &parameters->roi.x);
    if (strcmp(key, "roi_y") == 0) return assign_u16(value, &parameters->roi.y);
    if (strcmp(key, "roi_width") == 0) return assign_u16(value, &parameters->roi.width);
    if (strcmp(key, "roi_height") == 0) return assign_u16(value, &parameters->roi.height);
    if (strcmp(key, "foreground_threshold") == 0) return assign_u8(value, &parameters->foreground_threshold);
    if (strcmp(key, "foreground_is_bright") == 0) {
        if (strcmp(value, "true") == 0) parameters->foreground_is_bright = true;
        else if (strcmp(value, "false") == 0) parameters->foreground_is_bright = false;
        else return -1;
        return 0;
    }
    if (strcmp(key, "minimum_frame_mean") == 0) return assign_u8(value, &parameters->minimum_frame_mean);
    if (strcmp(key, "maximum_frame_mean") == 0) return assign_u8(value, &parameters->maximum_frame_mean);
    if (strcmp(key, "minimum_area_pixels") == 0) return parse_u32(value, UINT32_MAX, &parameters->minimum_area_pixels);
    if (strcmp(key, "maximum_area_pixels") == 0) return parse_u32(value, UINT32_MAX, &parameters->maximum_area_pixels);
    if (strcmp(key, "minimum_aspect_per_mille") == 0) return assign_u16(value, &parameters->minimum_aspect_per_mille);
    if (strcmp(key, "maximum_aspect_per_mille") == 0) return assign_u16(value, &parameters->maximum_aspect_per_mille);
    if (strcmp(key, "expected_center_x") == 0) return assign_u16(value, &parameters->expected_center_x);
    if (strcmp(key, "expected_center_y") == 0) return assign_u16(value, &parameters->expected_center_y);
    if (strcmp(key, "maximum_offset_pixels") == 0) return assign_u16(value, &parameters->maximum_offset_pixels);
    if (strcmp(key, "minimum_coverage_per_mille") == 0) return assign_u16(value, &parameters->minimum_coverage_per_mille);
    return -1;
}

static const char *const known_keys[] = {
    "parameter_version", "roi_x", "roi_y", "roi_width", "roi_height",
    "foreground_threshold", "foreground_is_bright", "minimum_frame_mean",
    "maximum_frame_mean", "minimum_area_pixels", "maximum_area_pixels",
    "minimum_aspect_per_mille", "maximum_aspect_per_mille",
    "expected_center_x", "expected_center_y", "maximum_offset_pixels",
    "minimum_coverage_per_mille",
};
#define KNOWN_KEY_COUNT (sizeof(known_keys) / sizeof(known_keys[0]))

int inspection_parameters_load_file(const char *path, InspectionParameters *parameters)
{
    char line[192];
    InspectionParameters loaded;
    bool seen[KNOWN_KEY_COUNT];
    FILE *file;
    size_t index;
    if (path == NULL || parameters == NULL) return -1;
    file = open_read_file(path);
    if (file == NULL) return -1;
    for (index = 0U; index < KNOWN_KEY_COUNT; ++index) seen[index] = false;
    loaded = inspection_default_parameters();
    while (fgets(line, sizeof(line), file) != NULL) {
        char *key = trim(line);
        char *separator;
        char *value;
        if (*key == '\0' || *key == '#') continue;
        separator = strchr(key, '=');
        if (separator == NULL) goto failure;
        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);
        if (*key == '\0' || *value == '\0') goto failure;
        for (index = 0U; index < KNOWN_KEY_COUNT; ++index) {
            if (strcmp(key, known_keys[index]) == 0) break;
        }
        if (index == KNOWN_KEY_COUNT) goto failure; /* 未知键 */
        if (seen[index]) goto failure;              /* 重复键 */
        seen[index] = true;
        if (set_parameter(&loaded, key, value) != 0) goto failure;
    }
    fclose(file);
    if (!seen[0] || loaded.version == 0U || loaded.minimum_frame_mean > loaded.maximum_frame_mean ||
        loaded.minimum_area_pixels > loaded.maximum_area_pixels ||
        loaded.minimum_aspect_per_mille > loaded.maximum_aspect_per_mille ||
        loaded.minimum_coverage_per_mille > 1000U) return -1;
    *parameters = loaded;
    return 0;

failure:
    fclose(file);
    return -1;
}
