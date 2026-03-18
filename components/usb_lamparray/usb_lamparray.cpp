#include "usb_lamparray.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <cmath>

// TinyUSB includes (available via ESPHome's framework = esp-idf)
#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char *const TAG = "usb_lamparray";

namespace esphome {
namespace usb_lamparray {

// ============================================================================
// Singleton
// ============================================================================
USBLampArrayComponent *USBLampArrayComponent::instance_ = nullptr;

// ============================================================================
// HID Report Descriptor for LampArray
//
// This encodes the complete capability advertisement to Windows.
// The descriptor follows HID Usage Tables 1.3 Section 26 (Lighting and
// Illumination) exactly as required by Windows Dynamic Lighting.
//
// Key points:
//   - Usage Page 0x59  = Lighting and Illumination
//   - The six report IDs map 1:1 to the structs in hid_lamparray_types.h
//   - All multi-byte fields are little-endian (USB spec)
// ============================================================================

// Helper macros to reduce verbosity
#define HID_USAGE_PAGE_LIGHTING    0x59
#define HID_USAGE_LAMP_ARRAY       0x01
#define HID_USAGE_LAMP_ARRAY_ATTRS 0x02
#define HID_USAGE_LAMP_COUNT       0x03
#define HID_USAGE_BOUNDING_BOX_W   0x04
#define HID_USAGE_BOUNDING_BOX_H   0x05
#define HID_USAGE_BOUNDING_BOX_D   0x06
#define HID_USAGE_LAMP_ARRAY_KIND  0x07
#define HID_USAGE_MIN_UPDATE_INT   0x08
#define HID_USAGE_LAMP_ATTRS_REQ   0x20
#define HID_USAGE_LAMP_ID          0x21
#define HID_USAGE_LAMP_ATTRS_RESP  0x28
#define HID_USAGE_POSITION_X       0x29
#define HID_USAGE_POSITION_Y       0x2A
#define HID_USAGE_POSITION_Z       0x2B
#define HID_USAGE_LAMP_PURPOSES    0x2C
#define HID_USAGE_UPDATE_LATENCY   0x2D
#define HID_USAGE_RED_LEVEL_COUNT  0x2E
#define HID_USAGE_GREEN_LEVEL_COUNT 0x2F
#define HID_USAGE_BLUE_LEVEL_COUNT 0x30
#define HID_USAGE_INTENSITY_COUNT  0x31
#define HID_USAGE_IS_PROGRAMMABLE  0x32
#define HID_USAGE_INPUT_BINDING    0x33
#define HID_USAGE_LAMP_MULTI_UPD   0x50
#define HID_USAGE_RED_UPDATE       0x51
#define HID_USAGE_GREEN_UPDATE     0x52
#define HID_USAGE_BLUE_UPDATE      0x53
#define HID_USAGE_INTENSITY_UPDATE 0x54
#define HID_USAGE_LAMP_UPDATE_FLAGS 0x55
#define HID_USAGE_LAMP_RANGE_UPD   0x60
#define HID_USAGE_LAMP_ID_START    0x61
#define HID_USAGE_LAMP_ID_END      0x62
#define HID_USAGE_LAMP_ARRAY_CTRL  0x70
#define HID_USAGE_AUTONOMOUS_MODE  0x71

// The raw HID report descriptor bytes.
// This is fixed-size and doesn't depend on lamp count at compile time —
// the actual lamp count is communicated via Report 0x01 at runtime.
static const uint8_t LAMP_ARRAY_DESCRIPTOR[] = {
  // ---- Top-level collection: LampArray ----
  0x05, HID_USAGE_PAGE_LIGHTING,          // Usage Page (Lighting and Illumination)
  0x09, HID_USAGE_LAMP_ARRAY,             // Usage (LampArray)
  0xA1, 0x01,                             // Collection (Application)

  // -- Report 0x01: LampArrayAttributesReport (Feature, device → host) --
  0x85, REPORT_ID_LAMP_ARRAY_ATTRIBUTES_REPORT,
  0x09, HID_USAGE_LAMP_ARRAY_ATTRS,
  0xA1, 0x02,                             // Collection (Logical)
    // LampCount: 1x uint16
    0x09, HID_USAGE_LAMP_COUNT,
    0x15, 0x01,                           // Logical Minimum (1)
    0x27, 0xFF, 0xFF, 0x00, 0x00,         // Logical Maximum (65535)
    0x75, 0x10,                           // Report Size (16)
    0x95, 0x01,                           // Report Count (1)
    0xB1, 0x03,                           // Feature (Const, Var, Abs)
    // BoundingBoxWidth, Height, Depth: 3x uint32
    0x09, HID_USAGE_BOUNDING_BOX_W,
    0x09, HID_USAGE_BOUNDING_BOX_H,
    0x09, HID_USAGE_BOUNDING_BOX_D,
    0x15, 0x00,                           // Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,         // Logical Maximum (2147483647)
    0x75, 0x20,                           // Report Size (32)
    0x95, 0x03,                           // Report Count (3)
    0xB1, 0x03,                           // Feature (Const, Var, Abs)
    // LampArrayKind: uint32
    0x09, HID_USAGE_LAMP_ARRAY_KIND,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x01,
    0xB1, 0x03,                           // Feature (Const, Var, Abs)
    // MinUpdateInterval: uint32
    0x09, HID_USAGE_MIN_UPDATE_INT,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x01,
    0xB1, 0x03,                           // Feature (Const, Var, Abs)
  0xC0,                                   // End Collection (LampArrayAttributes)

  // -- Report 0x02: LampAttributesRequestReport (Feature, host → device) --
  0x85, REPORT_ID_LAMP_ATTRIBUTES_REQUEST_REPORT,
  0x09, HID_USAGE_LAMP_ATTRS_REQ,
  0xA1, 0x02,
    0x09, HID_USAGE_LAMP_ID,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x01,
    0xB1, 0x02,                           // Feature (Data, Var, Abs)
  0xC0,

  // -- Report 0x03: LampAttributesResponseReport (Feature, device → host) --
  0x85, REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT,
  0x09, HID_USAGE_LAMP_ATTRS_RESP,
  0xA1, 0x02,
    // LampId: uint16
    0x09, HID_USAGE_LAMP_ID,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x01,
    0xB1, 0x03,
    // PositionX/Y/Z, UpdateLatency, LampPurposes: 5x uint32
    0x09, HID_USAGE_POSITION_X,
    0x09, HID_USAGE_POSITION_Y,
    0x09, HID_USAGE_POSITION_Z,
    0x09, HID_USAGE_UPDATE_LATENCY,
    0x09, HID_USAGE_LAMP_PURPOSES,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x05,
    0xB1, 0x03,
    // RedLevelCount, GreenLevelCount, BlueLevelCount, IntensityLevelCount,
    // IsProgrammable, InputBinding: 6x uint8
    0x09, HID_USAGE_RED_LEVEL_COUNT,
    0x09, HID_USAGE_GREEN_LEVEL_COUNT,
    0x09, HID_USAGE_BLUE_LEVEL_COUNT,
    0x09, HID_USAGE_INTENSITY_COUNT,
    0x09, HID_USAGE_IS_PROGRAMMABLE,
    0x09, HID_USAGE_INPUT_BINDING,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x06,
    0xB1, 0x03,
  0xC0,

  // -- Report 0x04: LampMultiUpdateReport (Output, host → device) --
  // Carries up to 8 lamp ID + colour pairs per report
  0x85, REPORT_ID_LAMP_MULTI_UPDATE_REPORT,
  0x09, HID_USAGE_LAMP_MULTI_UPD,
  0xA1, 0x02,
    // LampCount: uint8
    0x09, HID_USAGE_LAMP_COUNT,
    0x15, 0x00,
    0x25, 0x08,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,                           // Output (Data, Var, Abs)
    // LampUpdateFlags: uint8
    0x09, HID_USAGE_LAMP_UPDATE_FLAGS,
    0x15, 0x00,
    0x25, 0xFF,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,
    // Reserved: uint8
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x03,
    // 8x LampId: uint16 each
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x09, HID_USAGE_LAMP_ID,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x08,
    0x91, 0x02,
    // 8x Red, 8x Green, 8x Blue, 8x Intensity: each is 8 uint8s
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x20,                           // 32 fields (8 lamps x 4 channels)
    0x91, 0x02,
  0xC0,

  // -- Report 0x05: LampRangeUpdateReport (Output, host → device) --
  0x85, REPORT_ID_LAMP_RANGE_UPDATE_REPORT,
  0x09, HID_USAGE_LAMP_RANGE_UPD,
  0xA1, 0x02,
    // LampUpdateFlags: uint8
    0x09, HID_USAGE_LAMP_UPDATE_FLAGS,
    0x15, 0x00,
    0x25, 0xFF,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,
    // Reserved: 2 bytes
    0x75, 0x08,
    0x95, 0x02,
    0x91, 0x03,
    // LampIdStart, LampIdEnd: uint16 each
    0x09, HID_USAGE_LAMP_ID_START,
    0x09, HID_USAGE_LAMP_ID_END,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x02,
    0x91, 0x02,
    // Red, Green, Blue, Intensity: uint8 each
    0x09, HID_USAGE_RED_UPDATE,
    0x09, HID_USAGE_GREEN_UPDATE,
    0x09, HID_USAGE_BLUE_UPDATE,
    0x09, HID_USAGE_INTENSITY_UPDATE,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x04,
    0x91, 0x02,
  0xC0,

  // -- Report 0x06: LampArrayControlReport (Feature, host → device) --
  0x85, REPORT_ID_LAMP_ARRAY_CONTROL_REPORT,
  0x09, HID_USAGE_LAMP_ARRAY_CTRL,
  0xA1, 0x02,
    0x09, HID_USAGE_AUTONOMOUS_MODE,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x08,
    0x95, 0x01,
    0xB1, 0x02,
  0xC0,

  0xC0,                                   // End Collection (LampArray)
};

// ============================================================================
// TinyUSB HID descriptor and string tables
// ============================================================================

// Device descriptor — presented to Windows when the USB device enumerates
static tusb_desc_device_t s_device_descriptor = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,          // USB 2.0
  .bDeviceClass       = 0x00,            // Defined at interface level
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = 0x303A,          // filled in setup()
  .idProduct          = 0x4004,          // filled in setup()
  .bcdDevice          = 0x0100,
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01,
};

// HID interface + endpoint descriptor (full-speed, interrupt)
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID  0x81  // EP1 IN

static uint8_t s_config_descriptor[] = {
  // Config descriptor
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
  // HID Interface descriptor
  TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(LAMP_ARRAY_DESCRIPTOR), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10),
};

// String descriptor table
static const char *s_string_descriptor[5];

// ============================================================================
// TinyUSB C-linkage callbacks
// These are the hooks TinyUSB calls — they delegate to our singleton.
// ============================================================================

extern "C" {

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  return LAMP_ARRAY_DESCRIPTOR;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
  auto *comp = esphome::usb_lamparray::USBLampArrayComponent::instance();
  if (comp) return comp->on_get_report(report_id, buffer, reqlen);
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            const uint8_t *buffer, uint16_t buflen) {
  auto *comp = esphome::usb_lamparray::USBLampArrayComponent::instance();
  if (comp) comp->on_set_report(report_id, buffer, buflen);
}

// Required stub — we don't use HID Output reports via the IN endpoint
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t len) {}

} // extern "C"

// ============================================================================
// Component implementation
// ============================================================================

void USBLampArrayComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up USB LampArray...");
  ESP_LOGCONFIG(TAG, "  Lamps: %d", this->num_lamps_);
  ESP_LOGCONFIG(TAG, "  Kind:  0x%02X", (unsigned)this->lamp_array_kind_);

  instance_ = this;

  // Clamp to maximum
  if (this->num_lamps_ > USB_LAMPARRAY_MAX_LAMPS) {
    ESP_LOGW(TAG, "num_lamps clamped to %d", USB_LAMPARRAY_MAX_LAMPS);
    this->num_lamps_ = USB_LAMPARRAY_MAX_LAMPS;
  }

  // Initialise all lamp states to off
  memset(this->lamp_states_,  0, sizeof(LampState) * this->num_lamps_);
  memset(this->pending_states_, 0, sizeof(LampState) * this->num_lamps_);

  // Build the per-lamp attribute table
  this->build_lamp_attributes_();

  // Fill in configurable fields in the device descriptor
  s_device_descriptor.idVendor  = this->vendor_id_;
  s_device_descriptor.idProduct = this->product_id_;

  // String table (index 0 = language, 1 = manufacturer, 2 = product, 3 = serial)
  static char serial_buf[16];
  snprintf(serial_buf, sizeof(serial_buf), "%08X", (unsigned)ESP.getEfuseMac());
  s_string_descriptor[0] = (char[]){0x09, 0x04};  // English (US)
  s_string_descriptor[1] = this->manufacturer_;
  s_string_descriptor[2] = this->product_;
  s_string_descriptor[3] = serial_buf;
  s_string_descriptor[4] = nullptr;

  // Install TinyUSB driver
  tinyusb_config_t tusb_cfg = {
    .device_descriptor        = &s_device_descriptor,
    .string_descriptor        = s_string_descriptor,
    .string_descriptor_count  = 4,
    .external_phy             = false,
    .configuration_descriptor = s_config_descriptor,
  };
  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "USB LampArray ready — waiting for Windows to enumerate");
}

void USBLampArrayComponent::loop() {
  // If in autonomous mode, push the autonomous colour on first entry
  if (this->autonomous_mode_) {
    static bool autonomous_pushed = false;
    if (!autonomous_pushed) {
      for (int i = 0; i < this->num_lamps_; i++) {
        this->lamp_states_[i] = {this->autonomous_r_, this->autonomous_g_, this->autonomous_b_};
      }
      autonomous_pushed = true;
      this->dirty_ = true;
    }
  } else {
    autonomous_pushed = false;  // reset flag so next autonomous entry re-pushes
  }

  if (this->dirty_ && this->light_) {
    this->flush_to_light_();
    this->dirty_ = false;
  }
}

void USBLampArrayComponent::flush_to_light_() {
  auto &leds = *this->light_;
  for (int i = 0; i < this->num_lamps_ && i < leds.size(); i++) {
    leds[i] = light::ESPColor(
      this->lamp_states_[i].red,
      this->lamp_states_[i].green,
      this->lamp_states_[i].blue
    );
  }
  leds.schedule_show();
}

// ============================================================================
// Lamp attribute table construction
//
// We lay the lamps out in a circular arrangement in the XY plane (Z=0).
// This gives Windows Dynamic Lighting spatially-aware effects like "wave"
// that actually make sense on fan rings.
//
// Physical dimensions: ~120mm diameter fan ring → radius ~50mm → 50000 µm.
// ============================================================================
void USBLampArrayComponent::build_lamp_attributes_() {
  const float PI = 3.14159265f;
  // Bounding box: 120mm x 120mm x 10mm (typical 120mm fan ring)
  const uint32_t BOX_W = 120000, BOX_H = 120000, BOX_D = 10000;
  const uint32_t CX = BOX_W / 2, CY = BOX_H / 2;
  const uint32_t RADIUS = 50000;  // 50mm in micrometers

  for (uint16_t i = 0; i < this->num_lamps_; i++) {
    float angle = (2.0f * PI * i) / this->num_lamps_;
    uint32_t x = (uint32_t)(CX + RADIUS * cosf(angle));
    uint32_t y = (uint32_t)(CY + RADIUS * sinf(angle));

    LampAttributesResponseReport &a = this->lamp_attrs_[i];
    a.report_id           = REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT;
    a.lamp_id             = i;
    a.position_x          = x;
    a.position_y          = y;
    a.position_z          = 0;
    a.update_latency      = 0;
    a.lamp_purposes       = LAMP_PURPOSE_ACCENT;
    a.red_level_count     = 255;
    a.green_level_count   = 255;
    a.blue_level_count    = 255;
    a.intensity_level_count = 255;
    a.is_programmable     = 1;
    a.input_binding       = 0;
  }
}

// ============================================================================
// HID GET_REPORT handler
// Windows calls this with Feature requests to discover device capabilities.
// ============================================================================
uint16_t USBLampArrayComponent::on_get_report(uint8_t report_id, uint8_t *buffer, uint16_t req_len) {
  switch (report_id) {
    case REPORT_ID_LAMP_ARRAY_ATTRIBUTES_REPORT: {
      LampArrayAttributesReport r{};
      r.report_id           = report_id;
      r.lamp_count          = this->num_lamps_;
      r.bounding_box_width  = 120000;
      r.bounding_box_height = 120000;
      r.bounding_box_depth  = 10000;
      r.lamp_array_kind     = this->lamp_array_kind_;
      r.min_update_interval = 0;
      uint16_t len = (uint16_t)sizeof(r) - 1;  // exclude report_id byte (TinyUSB prepends it)
      memcpy(buffer, &r.lamp_count, len);
      return len;
    }

    case REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT: {
      // Return attributes for lamp requested via Report 0x02
      uint16_t id = this->requested_lamp_id_;
      if (id >= this->num_lamps_) id = 0;
      LampAttributesResponseReport &a = this->lamp_attrs_[id];
      uint16_t len = (uint16_t)sizeof(a) - 1;
      memcpy(buffer, &a.lamp_id, len);
      return len;
    }

    default:
      return 0;
  }
}

// ============================================================================
// HID SET_REPORT handler
// Windows sends colour updates and control commands here.
// ============================================================================
void USBLampArrayComponent::on_set_report(uint8_t report_id, const uint8_t *buffer, uint16_t buf_len) {
  switch (report_id) {

    case REPORT_ID_LAMP_ATTRIBUTES_REQUEST_REPORT: {
      // Host is asking: "give me attributes for lamp N on next GET_REPORT"
      if (buf_len >= sizeof(uint16_t)) {
        memcpy(&this->requested_lamp_id_, buffer, sizeof(uint16_t));
        ESP_LOGV(TAG, "Lamp attr request for lamp %d", this->requested_lamp_id_);
      }
      break;
    }

    case REPORT_ID_LAMP_MULTI_UPDATE_REPORT: {
      if (buf_len < 4) break;
      uint8_t count = buffer[0];
      uint8_t flags = buffer[1];
      // buffer[2] is reserved
      const uint8_t *data = buffer + 3;

      if (count > LAMP_MULTI_UPDATE_LAMP_COUNT) count = LAMP_MULTI_UPDATE_LAMP_COUNT;

      // Lamp IDs start at offset 0 within data (8x uint16)
      const uint16_t *ids = (const uint16_t *)(data);
      // Colour data starts after 8x uint16 (16 bytes) of IDs
      // Layout: 8 red bytes, 8 green bytes, 8 blue bytes, 8 intensity bytes
      const uint8_t *reds       = data + 16;
      const uint8_t *greens     = data + 24;
      const uint8_t *blues      = data + 32;
      const uint8_t *intensities = data + 40;

      for (uint8_t i = 0; i < count; i++) {
        uint16_t id = ids[i];
        if (id >= this->num_lamps_) continue;
        uint8_t intens = intensities[i];
        // Apply intensity as a multiplier (255 = full, 0 = off)
        float scale = (intens == 255) ? 1.0f : (intens / 255.0f);
        this->pending_states_[id].red   = (uint8_t)(reds[i]   * scale);
        this->pending_states_[id].green = (uint8_t)(greens[i] * scale);
        this->pending_states_[id].blue  = (uint8_t)(blues[i]  * scale);
      }
      this->has_pending_ = true;

      // LampUpdateComplete flag means "commit the buffered frame now"
      if (flags & LAMP_UPDATE_FLAG_COMPLETE) {
        memcpy(this->lamp_states_, this->pending_states_, sizeof(LampState) * this->num_lamps_);
        this->dirty_ = true;
      }
      break;
    }

    case REPORT_ID_LAMP_RANGE_UPDATE_REPORT: {
      if (buf_len < sizeof(LampRangeUpdateReport) - 1) break;
      // Reconstruct struct (buffer doesn't include the report_id byte)
      LampRangeUpdateReport r{};
      r.report_id = report_id;
      memcpy(&r.lamp_update_flags, buffer, buf_len);

      uint16_t start = r.lamp_id_start;
      uint16_t end   = r.lamp_id_end;
      if (end >= this->num_lamps_) end = this->num_lamps_ - 1;

      float scale = (r.color.intensity == 255) ? 1.0f : (r.color.intensity / 255.0f);
      for (uint16_t i = start; i <= end; i++) {
        this->pending_states_[i].red   = (uint8_t)(r.color.red   * scale);
        this->pending_states_[i].green = (uint8_t)(r.color.green * scale);
        this->pending_states_[i].blue  = (uint8_t)(r.color.blue  * scale);
      }
      this->has_pending_ = true;

      if (r.lamp_update_flags & LAMP_UPDATE_FLAG_COMPLETE) {
        memcpy(this->lamp_states_, this->pending_states_, sizeof(LampState) * this->num_lamps_);
        this->dirty_ = true;
      }
      break;
    }

    case REPORT_ID_LAMP_ARRAY_CONTROL_REPORT: {
      if (buf_len >= 1) {
        bool was_autonomous = this->autonomous_mode_;
        this->autonomous_mode_ = (buffer[0] != 0);
        if (was_autonomous && !this->autonomous_mode_) {
          ESP_LOGI(TAG, "Host took control of LampArray");
          // Clear to black so host starts fresh
          memset(this->lamp_states_,   0, sizeof(LampState) * this->num_lamps_);
          memset(this->pending_states_, 0, sizeof(LampState) * this->num_lamps_);
          this->dirty_ = true;
        } else if (!was_autonomous && this->autonomous_mode_) {
          ESP_LOGI(TAG, "LampArray returned to autonomous mode");
        }
      }
      break;
    }

    default:
      ESP_LOGV(TAG, "Unknown SET_REPORT id=0x%02X", report_id);
      break;
  }
}

}  // namespace usb_lamparray
}  // namespace esphome
