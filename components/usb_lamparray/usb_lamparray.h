#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/light/addressable_light.h"
#include "hid_lamparray_types.h"

#include "tinyusb.h"
#include "class/hid/hid_device.h"

namespace esphome {
namespace usb_lamparray {

// Maximum lamps this component supports
#define USB_LAMPARRAY_MAX_LAMPS 256

// LampUpdateComplete flag in multi/range update reports
#define LAMP_UPDATE_FLAG_COMPLETE 0x01

class USBLampArrayComponent : public Component {
 public:
  // ---- ESPHome lifecycle ----
  void setup() override;
  void loop() override;
  //float get_setup_priority() const override { return setup_priority::HARDWARE; }
  float get_setup_priority() const override { return esphome::setup_priority::HARDWARE; }
  // ---- Configuration setters (called from __init__.py generated code) ----
  void set_num_lamps(uint16_t count)             { this->num_lamps_ = count; }
  void set_lamp_array_kind(uint32_t kind)        { this->lamp_array_kind_ = kind; }
  void set_light(light::LightState *state) {
    this->light_ = static_cast<light::AddressableLight *>(state->get_output());
  }
  void set_vendor_id(uint16_t vid)               { this->vendor_id_ = vid; }
  void set_product_id(uint16_t pid)              { this->product_id_ = pid; }
  void set_manufacturer(const char *s)           { this->manufacturer_ = s; }
  void set_product(const char *s)                { this->product_ = s; }
  void set_autonomous_mode_color(uint8_t r, uint8_t g, uint8_t b) {
    this->autonomous_r_ = r;
    this->autonomous_g_ = g;
    this->autonomous_b_ = b;
  }

  // ---- TinyUSB HID callbacks (called from C shim, must be public) ----
  uint16_t on_get_report(uint8_t report_id, uint8_t *buffer, uint16_t req_len);
  void     on_set_report(uint8_t report_id, const uint8_t *buffer, uint16_t buf_len);

  // Singleton accessor (needed for C-linkage TinyUSB callbacks)
  static USBLampArrayComponent *instance() { return instance_; }

 protected:
  // Build per-lamp position/attribute data based on lamp count
  void build_lamp_attributes_();

  bool autonomous_pushed_{false};

  // Push current lamp_states_ to the ESPHome light component
  void flush_to_light_();

  // Configuration
  uint16_t     num_lamps_{16};
  uint32_t     lamp_array_kind_{LAMP_ARRAY_KIND_PERIPHERAL};
  light::AddressableLight *light_{nullptr};
  uint16_t     vendor_id_{0x303A};   // Espressif default VID
  uint16_t     product_id_{0x4004};  // arbitrary HID PID
  const char  *manufacturer_{"ESPHome"};
  const char  *product_{"USB LampArray"};

  // Autonomous mode fallback colour (shown when no app is controlling)
  uint8_t autonomous_r_{0};
  uint8_t autonomous_g_{0};
  uint8_t autonomous_b_{20};  // dim blue default

  // Runtime state
  bool        autonomous_mode_{true};
  bool        dirty_{false};          // true when lamp_states_ has unwritten changes
  uint16_t    requested_lamp_id_{0};  // tracks GET_REPORT(LampAttributesResponse) cursor
  LampState   lamp_states_[USB_LAMPARRAY_MAX_LAMPS]{};

  // Pending buffered colours (we only flush when LampUpdateComplete flag is set)
  LampState   pending_states_[USB_LAMPARRAY_MAX_LAMPS]{};
  bool        has_pending_{false};

  // Per-lamp attribute table (positions computed from lamp count in a circle)
  LampAttributesResponseReport lamp_attrs_[USB_LAMPARRAY_MAX_LAMPS]{};

  static USBLampArrayComponent *instance_;
};

}  // namespace usb_lamparray
}  // namespace esphome
