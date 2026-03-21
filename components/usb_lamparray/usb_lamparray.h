#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/addressable_light.h"
#include "hid_lamparray_types.h"
#include <string>

namespace esphome {
namespace usb_lamparray {

// Maximum lamps supported (Windows Dynamic Lighting limit is 256)
static constexpr uint16_t USB_LAMPARRAY_MAX_LAMPS = 256;

// ============================================================================
// USBLampArrayComponent
//
// Implements a USB HID LampArray device using TinyUSB on the ESP32-S3.
// All TinyUSB descriptor callbacks are in usb_lamparray.cpp (extern "C").
// This class owns:
//   - The lamp attribute table (positions, capabilities)
//   - The current and pending lamp colour state
//   - Flushing colour state to the ESPHome addressable light
// ============================================================================
class USBLampArrayComponent : public Component {
 public:
  // ── ESPHome Component lifecycle ─────────────────────────────────────────
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // ── Singleton access (used by TinyUSB C callbacks) ───────────────────────
  static USBLampArrayComponent *instance() { return instance_; }

  // ── YAML setters (called from __init__.py generated code) ────────────────
  void set_num_lamps(uint16_t n)           { num_lamps_ = n; }
  void set_lamp_array_kind(uint32_t kind)  { lamp_array_kind_ = kind; }
  void set_light(light::AddressableLight *l) { light_ = l; }
  void set_vendor_id(uint16_t vid)         { vendor_id_ = vid; }
  void set_product_id(uint16_t pid)        { product_id_ = pid; }
  void set_manufacturer(const char *s)     { manufacturer_ = s; }
  void set_product(const char *s)          { product_ = s; }
  void set_autonomous_mode_color(uint8_t r, uint8_t g, uint8_t b) {
    autonomous_r_ = r;
    autonomous_g_ = g;
    autonomous_b_ = b;
  }

  // ── Getters used by TinyUSB string descriptor callback ──────────────────
  const char *get_manufacturer() const { return manufacturer_.c_str(); }
  const char *get_product()      const { return product_.c_str(); }

  // ── HID report handlers (called from extern "C" TinyUSB callbacks) ───────
  uint16_t on_get_report(uint8_t report_id, uint8_t *buffer, uint16_t req_len);
  void     on_set_report(uint8_t report_id, const uint8_t *buffer, uint16_t buf_len);

 protected:
  // ── Internal helpers ─────────────────────────────────────────────────────
  void build_lamp_attributes_();
  void flush_to_light_();

  // ── Singleton ────────────────────────────────────────────────────────────
  static USBLampArrayComponent *instance_;

  // ── Configuration ────────────────────────────────────────────────────────
  uint16_t num_lamps_       = 1;
  uint32_t lamp_array_kind_ = 0x04;   // peripheral
  uint16_t vendor_id_       = 0x303A;
  uint16_t product_id_      = 0x4004;
  std::string manufacturer_ = "ESPHome";
  std::string product_      = "USB LampArray";

  // Colour shown when no Windows app is controlling the device
  uint8_t autonomous_r_ = 0;
  uint8_t autonomous_g_ = 0;
  uint8_t autonomous_b_ = 20;

  // ── Runtime state ────────────────────────────────────────────────────────
  light::AddressableLight *light_ = nullptr;

  // Lamp attribute table — built once in setup()
  LampAttributesResponseReport lamp_attrs_[USB_LAMPARRAY_MAX_LAMPS]{};

  // Committed colours (shown on LEDs) and buffered-but-not-yet-committed
  struct LampState { uint8_t red, green, blue; };
  LampState lamp_states_[USB_LAMPARRAY_MAX_LAMPS]{};
  LampState pending_states_[USB_LAMPARRAY_MAX_LAMPS]{};

  uint16_t requested_lamp_id_ = 0;   // lamp whose attrs were last requested
  bool     autonomous_mode_   = true; // true = device controls its own LEDs
  bool     dirty_             = false;
  bool     has_pending_       = false;
};

}  // namespace usb_lamparray
}  // namespace esphome
