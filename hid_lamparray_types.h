#pragma once
#include <stdint.h>

// ============================================================================
// HID LampArray Types
// Based on USB HID Usage Tables for Lighting and Illumination (Section 26)
// and Microsoft Dynamic Lighting specification
// ============================================================================

// LampArrayKind values (Usage 0x01 in Lighting and Illumination page)
#define LAMP_ARRAY_KIND_UNDEFINED     0x00
#define LAMP_ARRAY_KIND_KEYBOARD      0x01
#define LAMP_ARRAY_KIND_MOUSE         0x02
#define LAMP_ARRAY_KIND_GAME_CONTROLLER 0x03
#define LAMP_ARRAY_KIND_PERIPHERAL    0x04
#define LAMP_ARRAY_KIND_SCENE         0x05
#define LAMP_ARRAY_KIND_NOTIFICATION  0x06
#define LAMP_ARRAY_KIND_CHASSIS       0x07
#define LAMP_ARRAY_KIND_WEARABLE      0x08
#define LAMP_ARRAY_KIND_FURNITURE     0x09
#define LAMP_ARRAY_KIND_ART           0x0A

// LampPurpose flags (bitmask)
#define LAMP_PURPOSE_CONTROL          0x01
#define LAMP_PURPOSE_ACCENT           0x02
#define LAMP_PURPOSE_BRANDING         0x04
#define LAMP_PURPOSE_STATUS           0x08
#define LAMP_PURPOSE_ILLUMINATION     0x10
#define LAMP_PURPOSE_PRESENTATION     0x20

// Report IDs
#define REPORT_ID_LAMP_ARRAY_ATTRIBUTES_REPORT    0x01
#define REPORT_ID_LAMP_ATTRIBUTES_REQUEST_REPORT  0x02
#define REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT 0x03
#define REPORT_ID_LAMP_MULTI_UPDATE_REPORT        0x04
#define REPORT_ID_LAMP_RANGE_UPDATE_REPORT        0x05
#define REPORT_ID_LAMP_ARRAY_CONTROL_REPORT       0x06

// LampMultiUpdate can carry up to 8 lamps per report
#define LAMP_MULTI_UPDATE_LAMP_COUNT 8

// ============================================================================
// Report Structures (packed, as they go over USB)
// ============================================================================

#pragma pack(push, 1)

typedef struct {
  uint8_t  red;
  uint8_t  green;
  uint8_t  blue;
  uint8_t  intensity;  // overall brightness multiplier (255 = full)
} LampArrayColor;

// Report 0x01 - Device sends this to describe the overall array
typedef struct {
  uint8_t  report_id;            // = REPORT_ID_LAMP_ARRAY_ATTRIBUTES_REPORT
  uint16_t lamp_count;           // total number of lamps
  uint32_t bounding_box_width;   // micrometers
  uint32_t bounding_box_height;  // micrometers
  uint32_t bounding_box_depth;   // micrometers
  uint32_t lamp_array_kind;
  uint32_t min_update_interval;  // microseconds
} LampArrayAttributesReport;

// Report 0x02 - Host sends this to request attributes for a specific lamp index
typedef struct {
  uint8_t  report_id;  // = REPORT_ID_LAMP_ATTRIBUTES_REQUEST_REPORT
  uint16_t lamp_id;
} LampAttributesRequestReport;

// Report 0x03 - Device sends this in response to a request for lamp attributes
typedef struct {
  uint8_t  report_id;        // = REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT
  uint16_t lamp_id;
  uint32_t position_x;       // micrometers from origin
  uint32_t position_y;
  uint32_t position_z;
  uint32_t update_latency;   // microseconds
  uint32_t lamp_purposes;    // bitmask of LAMP_PURPOSE_*
  uint8_t  red_level_count;  // colour depth - 255 for full 8-bit
  uint8_t  green_level_count;
  uint8_t  blue_level_count;
  uint8_t  intensity_level_count;
  uint8_t  is_programmable;  // 1 = can be individually addressed
  uint8_t  input_binding;    // 0 = no keyboard binding
} LampAttributesResponseReport;

// Report 0x04 - Host sends RGB values for up to 8 lamps at once
typedef struct {
  uint8_t          report_id;      // = REPORT_ID_LAMP_MULTI_UPDATE_REPORT
  uint8_t          lamp_count;     // how many lamps in this report (1-8)
  uint8_t          lamp_update_flags; // bit 0 = apply immediately (LampUpdateComplete)
  uint8_t          _reserved;
  uint16_t         lamp_ids[LAMP_MULTI_UPDATE_LAMP_COUNT];
  LampArrayColor   lamp_colors[LAMP_MULTI_UPDATE_LAMP_COUNT];
} LampMultiUpdateReport;

// Report 0x05 - Host sends one colour to apply to a range of lamps
typedef struct {
  uint8_t        report_id;    // = REPORT_ID_LAMP_RANGE_UPDATE_REPORT
  uint8_t        lamp_update_flags;
  uint8_t        _reserved[2];
  uint16_t       lamp_id_start;
  uint16_t       lamp_id_end;
  LampArrayColor color;
} LampRangeUpdateReport;

// Report 0x06 - Host sets autonomous vs controlled mode
typedef struct {
  uint8_t report_id;           // = REPORT_ID_LAMP_ARRAY_CONTROL_REPORT
  uint8_t autonomous_mode;     // 1 = device runs own effects, 0 = host controlled
} LampArrayControlReport;

#pragma pack(pop)

// Internal state for a single lamp (what we'll use at runtime)
typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} LampState;
