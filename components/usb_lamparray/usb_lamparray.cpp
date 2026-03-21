#include "usb_lamparray.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <cmath>
#include "esp_mac.h"

// TinyUSB — available via ESPHome framework = esp-idf
#include "tinyusb.h"
#include "tusb.h"
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
// Encodes the full capability advertisement to Windows.
// Follows HID Usage Tables 1.3 §26 (Lighting and Illumination) exactly as
// required by Windows Dynamic Lighting.
//
// Key points:
//   - Usage Page 0x59 = Lighting and Illumination
//   - Six report IDs map 1:1 to structs in hid_lamparray_types.h
//   - All multi-byte fields are little-endian (USB spec)
//   - Lamp count is fixed in the descriptor; actual count sent via Report 0x01
// ============================================================================

#define HID_USAGE_PAGE_LIGHTING     0x59
#define HID_USAGE_LAMP_ARRAY        0x01
#define HID_USAGE_LAMP_ARRAY_ATTRS  0x02
#define HID_USAGE_LAMP_COUNT        0x03
#define HID_USAGE_BOUNDING_BOX_W    0x04
#define HID_USAGE_BOUNDING_BOX_H    0x05
#define HID_USAGE_BOUNDING_BOX_D    0x06
#define HID_USAGE_LAMP_ARRAY_KIND   0x07
#define HID_USAGE_MIN_UPDATE_INT    0x08
#define HID_USAGE_LAMP_ATTRS_REQ    0x20
#define HID_USAGE_LAMP_ID           0x21
#define HID_USAGE_LAMP_ATTRS_RESP   0x28
#define HID_USAGE_POSITION_X        0x29
#define HID_USAGE_POSITION_Y        0x2A
#define HID_USAGE_POSITION_Z        0x2B
#define HID_USAGE_LAMP_PURPOSES     0x2C
#define HID_USAGE_UPDATE_LATENCY    0x2D
#define HID_USAGE_RED_LEVEL_COUNT   0x2E
#define HID_USAGE_GREEN_LEVEL_COUNT 0x2F
#define HID_USAGE_BLUE_LEVEL_COUNT  0x30
#define HID_USAGE_INTENSITY_COUNT   0x31
#define HID_USAGE_IS_PROGRAMMABLE   0x32
#define HID_USAGE_INPUT_BINDING     0x33
#define HID_USAGE_LAMP_MULTI_UPD    0x50
#define HID_USAGE_RED_UPDATE        0x51
#define HID_USAGE_GREEN_UPDATE      0x52
#define HID_USAGE_BLUE_UPDATE       0x53
#define HID_USAGE_INTENSITY_UPDATE  0x54
#define HID_USAGE_LAMP_UPDATE_FLAGS 0x55
#define HID_USAGE_LAMP_RANGE_UPD    0x60
#define HID_USAGE_LAMP_ID_START     0x61
#define HID_USAGE_LAMP_ID_END       0x62
#define HID_USAGE_LAMP_ARRAY_CTRL   0x70
#define HID_USAGE_AUTONOMOUS_MODE   0x71

static const uint8_t LAMP_ARRAY_DESCRIPTOR[] = {
  // ---- Top-level collection: LampArray ----
  0x05, HID_USAGE_PAGE_LIGHTING,          // Usage Page (Lighting and Illumination)
  0x09, HID_USAGE_LAMP_ARRAY,             // Usage (LampArray)
  0xA1, 0x01,                             // Collection (Application)

  // -- Report 0x01: LampArrayAttributesReport (Feature, device → host) --
  0x85, REPORT_ID_LAMP_ARRAY_ATTRIBUTES_REPORT,
  0x09, HID_USAGE_LAMP_ARRAY_ATTRS,
  0xA1, 0x02,                             // Collection (Logical)
    0x09, HID_USAGE_LAMP_COUNT,
    0x15, 0x01,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x01,
    0xB1, 0x03,                           // Feature (Const, Var, Abs)
    0x09, HID_USAGE_BOUNDING_BOX_W,
    0x09, HID_USAGE_BOUNDING_BOX_H,
    0x09, HID_USAGE_BOUNDING_BOX_D,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x03,
    0xB1, 0x03,
    0x09, HID_USAGE_LAMP_ARRAY_KIND,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x01,
    0xB1, 0x03,
    0x09, HID_USAGE_MIN_UPDATE_INT,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,
    0x75, 0x20,
    0x95, 0x01,
    0xB1, 0x03,
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
    0xB1, 0x02,
  0xC0,

  // -- Report 0x03: LampAttributesResponseReport (Feature, device → host) --
  0x85, REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT,
  0x09, HID_USAGE_LAMP_ATTRS_RESP,
  0xA1, 0x02,
    0x09, HID_USAGE_LAMP_ID,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x01,
    0xB1, 0x03,
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
  0x85, REPORT_ID_LAMP_MULTI_UPDATE_REPORT,
  0x09, HID_USAGE_LAMP_MULTI_UPD,
  0xA1, 0x02,
    0x09, HID_USAGE_LAMP_COUNT,
    0x15, 0x00,
    0x25, 0x08,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,
    0x09, HID_USAGE_LAMP_UPDATE_FLAGS,
    0x15, 0x00,
    0x25, 0xFF,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x03,                           // Reserved
    // 8x LampId (uint16 each)
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
    // 8x Red, 8x Green, 8x Blue, 8x Intensity
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
    0x95, 0x20,                           // 32 fields (8 lamps × 4 channels)
    0x91, 0x02,
  0xC0,

  // -- Report 0x05: LampRangeUpdateReport (Output, host → device) --
  0x85, REPORT_ID_LAMP_RANGE_UPDATE_REPORT,
  0x09, HID_USAGE_LAMP_RANGE_UPD,
  0xA1, 0x02,
    0x09, HID_USAGE_LAMP_UPDATE_FLAGS,
    0x15, 0x00,
    0x25, 0xFF,
    0x75, 0x08,
    0x95, 0x01,
    0x91, 0x02,
    0x75, 0x08,
    0x95, 0x02,
    0x91, 0x03,                           // Reserved (2 bytes)
    0x09, HID_USAGE_LAMP_ID_START,
    0x09, HID_USAGE_LAMP_ID_END,
    0x15, 0x00,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,
    0x95, 0x02,
    0x91, 0x02,
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
// USB descriptor constants
// ============================================================================
#define _CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define _EPNUM_HID         0x81            // EP1 IN (interrupt)

// ============================================================================
// TinyUSB C-linkage callbacks
//
// We own ALL descriptor callbacks here because ESPHome's built-in tinyusb
// component does not support HID. This means fan-leds.yaml must NOT have a
// `tinyusb:` block and __init__.py must NOT list "tinyusb" in DEPENDENCIES —
// otherwise there will be duplicate symbol linker errors.
// ============================================================================

extern "C" {

// ----------------------------------------------------------------------------
// USB Device Descriptor
// bDeviceClass = 0x00: class is defined at interface level (required for HID)
// ----------------------------------------------------------------------------
static const tusb_desc_device_t s_device_desc = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = 0x00,
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = 0x303A,           // Espressif default VID
  .idProduct          = 0x4004,
  .bcdDevice          = 0x0100,
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&s_device_desc;
}

// ----------------------------------------------------------------------------
// USB Configuration Descriptor
// TUD_HID_DESCRIPTOR wires in our HID interface + EP1 IN
// ----------------------------------------------------------------------------
static const uint8_t s_config_desc[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, _CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_HID_DESCRIPTOR(
    0,                              // Interface number
    0,                              // String index (none)
    HID_ITF_PROTOCOL_NONE,          // Not a boot-protocol keyboard/mouse
    sizeof(LAMP_ARRAY_DESCRIPTOR),  // HID report descriptor length
    _EPNUM_HID,                     // EP1 IN
    64,                             // Max packet size
    5                               // Polling interval (ms)
  )
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return s_config_desc;
}

// ----------------------------------------------------------------------------
// USB String Descriptors
// 0 = language list, 1 = manufacturer, 2 = product, 3 = serial number
// ----------------------------------------------------------------------------
static uint16_t s_str_buf[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  const char *str = nullptr;
  uint8_t len;

  if (index == 0) {
    s_str_buf[1] = 0x0409;          // English (US)
    len = 1;
  } else {
    auto *comp = esphome::usb_lamparray::USBLampArrayComponent::instance();
    switch (index) {
      case 1:
        str = (comp && comp->get_manufacturer()[0])
              ? comp->get_manufacturer() : "ESPHome";
        break;
      case 2:
        str = (comp && comp->get_product()[0])
              ? comp->get_product() : "USB LampArray";
        break;
      case 3:
        str = "000001";
        break;
      default:
        return nullptr;
    }
    len = (uint8_t)strlen(str);
    if (len > 31) len = 31;
    for (uint8_t i = 0; i < len; i++)
      s_str_buf[1 + i] = str[i];    // ASCII → UTF-16LE
  }

  // High byte = descriptor type (0x03), low byte = total length in bytes
  s_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return s_str_buf;
}

// ----------------------------------------------------------------------------
// HID Report Descriptor callback
// Called by TinyUSB when the host requests GET_DESCRIPTOR(Report) on the HID
// interface. Must return the same descriptor used to size s_config_desc above.
// ----------------------------------------------------------------------------
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return LAMP_ARRAY_DESCRIPTOR;
}

// ----------------------------------------------------------------------------
// HID GET_REPORT callback
// Windows sends Feature requests here to discover device capabilities.
// ----------------------------------------------------------------------------
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
  (void)instance;
  (void)report_type;
  auto *comp = esphome::usb_lamparray::USBLampArrayComponent::instance();
  if (comp) return comp->on_get_report(report_id, buffer, reqlen);
  return 0;
}

// ----------------------------------------------------------------------------
// HID SET_REPORT callback
// Windows sends colour updates and mode-control commands here.
// ----------------------------------------------------------------------------
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            const uint8_t *buffer, uint16_t buflen) {
  (void)instance;
  (void)report_type;
  auto *comp = esphome::usb_lamparray::USBLampArrayComponent::instance();
  if (comp) comp->on_set_report(report_id, buffer, buflen);
}

// Required stub — no action needed when an IN report completes
void tud_hid_report_complete_cb(uint8_t instance,
                                 const uint8_t *report, uint16_t len) {
  (void)instance; (void)report; (void)len;
}

} // extern "C"

// ============================================================================
// Component implementation
// ============================================================================

void USBLampArrayComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up USB LampArray...");
  ESP_LOGCONFIG(TAG, "  Lamps: %d", this->num_lamps_);

  instance_ = this;

  if (this->num_lamps_ > USB_LAMPARRAY_MAX_LAMPS) {
    ESP_LOGW(TAG, "num_lamps clamped to %d", USB_LAMPARRAY_MAX_LAMPS);
    this->num_lamps_ = USB_LAMPARRAY_MAX_LAMPS;
  }

  memset(this->lamp_states_,    0, sizeof(LampState) * this->num_lamps_);
  memset(this->pending_states_, 0, sizeof(LampState) * this->num_lamps_);

  this->build_lamp_attributes_();

  ESP_LOGI(TAG, "USB LampArray ready");
}

void USBLampArrayComponent::loop() {
  if (this->autonomous_mode_) {
    static bool autonomous_pushed_ = false;
    if (!autonomous_pushed_) {
      for (int i = 0; i < this->num_lamps_; i++) {
        this->lamp_states_[i] = {
          this->autonomous_r_,
          this->autonomous_g_,
          this->autonomous_b_
        };
      }
      autonomous_pushed_ = true;
      this->dirty_ = true;
    }
  } else {
    autonomous_pushed_ = false;
  }

  if (this->dirty_ && this->light_) {
    this->flush_to_light_();
    this->dirty_ = false;
  }
}

void USBLampArrayComponent::flush_to_light_() {
  auto &leds = *this->light_;
  for (int i = 0; i < this->num_lamps_ && i < leds.size(); i++) {
    leds[i] = Color(
      this->lamp_states_[i].red,
      this->lamp_states_[i].green,
      this->lamp_states_[i].blue
    );
  }
  leds.schedule_show();
}

// ============================================================================
// Lamp attribute table
//
// Lamps are laid out in a circle in the XY plane (Z=0) to match a fan ring.
// Physical dimensions: 120mm fan → radius ~50mm → 50000 µm.
// This gives Windows Dynamic Lighting spatially-correct wave/sweep effects.
// ============================================================================
void USBLampArrayComponent::build_lamp_attributes_() {
  const float PI = 3.14159265f;
  const uint32_t BOX_W = 120000, BOX_H = 120000;
  const uint32_t CX = BOX_W / 2, CY = BOX_H / 2;
  const uint32_t RADIUS = 50000;

  for (uint16_t i = 0; i < this->num_lamps_; i++) {
    float angle = (2.0f * PI * i) / this->num_lamps_;
    uint32_t x = (uint32_t)(CX + RADIUS * cosf(angle));
    uint32_t y = (uint32_t)(CY + RADIUS * sinf(angle));

    LampAttributesResponseReport &a = this->lamp_attrs_[i];
    a.report_id             = REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT;
    a.lamp_id               = i;
    a.position_x            = x;
    a.position_y            = y;
    a.position_z            = 0;
    a.update_latency        = 0;
    a.lamp_purposes         = LAMP_PURPOSE_ACCENT;
    a.red_level_count       = 255;
    a.green_level_count     = 255;
    a.blue_level_count      = 255;
    a.intensity_level_count = 255;
    a.is_programmable       = 1;
    a.input_binding         = 0;
  }
}

// ============================================================================
// HID GET_REPORT handler
// ============================================================================
uint16_t USBLampArrayComponent::on_get_report(uint8_t report_id,
                                               uint8_t *buffer,
                                               uint16_t req_len) {
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
      // TinyUSB prepends the report_id byte, so exclude it from the copy
      uint16_t len = (uint16_t)sizeof(r) - 1;
      memcpy(buffer, &r.lamp_count, len);
      return len;
    }

    case REPORT_ID_LAMP_ATTRIBUTES_RESPONSE_REPORT: {
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
// ============================================================================
void USBLampArrayComponent::on_set_report(uint8_t report_id,
                                           const uint8_t *buffer,
                                           uint16_t buf_len) {
  switch (report_id) {

    case REPORT_ID_LAMP_ATTRIBUTES_REQUEST_REPORT: {
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

      if (count > LAMP_MULTI_UPDATE_LAMP_COUNT)
        count = LAMP_MULTI_UPDATE_LAMP_COUNT;

      const uint16_t *ids       = (const uint16_t *)(data);
      const uint8_t  *reds      = data + 16;  // after 8x uint16 IDs
      const uint8_t  *greens    = data + 24;
      const uint8_t  *blues     = data + 32;
      const uint8_t  *intensities = data + 40;

      for (uint8_t i = 0; i < count; i++) {
        uint16_t id = ids[i];
        if (id >= this->num_lamps_) continue;
        float scale = (intensities[i] == 255) ? 1.0f : (intensities[i] / 255.0f);
        this->pending_states_[id].red   = (uint8_t)(reds[i]   * scale);
        this->pending_states_[id].green = (uint8_t)(greens[i] * scale);
        this->pending_states_[id].blue  = (uint8_t)(blues[i]  * scale);
      }
      this->has_pending_ = true;

      if (flags & LAMP_UPDATE_FLAG_COMPLETE) {
        memcpy(this->lamp_states_, this->pending_states_,
               sizeof(LampState) * this->num_lamps_);
        this->dirty_ = true;
      }
      break;
    }

    case REPORT_ID_LAMP_RANGE_UPDATE_REPORT: {
      if (buf_len < sizeof(LampRangeUpdateReport) - 1) break;
      LampRangeUpdateReport r{};
      r.report_id = report_id;
      memcpy(&r.lamp_update_flags, buffer, buf_len);

      uint16_t start = r.lamp_id_start;
      uint16_t end   = r.lamp_id_end;
      if (end >= this->num_lamps_) end = this->num_lamps_ - 1;

      float scale = (r.color.intensity == 255) ? 1.0f
                                                : (r.color.intensity / 255.0f);
      for (uint16_t i = start; i <= end; i++) {
        this->pending_states_[i].red   = (uint8_t)(r.color.red   * scale);
        this->pending_states_[i].green = (uint8_t)(r.color.green * scale);
        this->pending_states_[i].blue  = (uint8_t)(r.color.blue  * scale);
      }
      this->has_pending_ = true;

      if (r.lamp_update_flags & LAMP_UPDATE_FLAG_COMPLETE) {
        memcpy(this->lamp_states_, this->pending_states_,
               sizeof(LampState) * this->num_lamps_);
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
          memset(this->lamp_states_,    0, sizeof(LampState) * this->num_lamps_);
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
