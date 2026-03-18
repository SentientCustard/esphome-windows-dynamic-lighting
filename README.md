# esphome-usb-lamparray

An ESPHome external component that turns an **ESP32-S3** into a **Windows Dynamic Lighting** compatible USB HID LampArray device.

Plug it into your PC — Windows sees it as a native RGB peripheral and you can control it from Settings → Personalisation → Dynamic Lighting, or from any app that uses the `Windows.Devices.Lights` API (Razer Synapse, ASUS Aura, Corsair iCUE, etc. all support the open standard).

## Hardware

| Item | Detail |
|------|--------|
| **MCU** | Waveshare ESP32-S3-Zero (or any ESP32-S3 with USB-C wired to OTG pins GPIO19/GPIO20) |
| **LEDs** | WS2812B (GRB order) — tested, any NeoPixel variant supported |
| **USB** | The board's onboard USB-C port — plug directly to your PC |
| **LED power** | 5V from PC USB header or SATA adapter; share GND with the S3-Zero |

### Wiring

```
PC USB-C ──────────────────────────── ESP32-S3-Zero USB-C
                                               │
                                        ┌──────┴──────┐
                                        │  S3-Zero    │
                                        │             │
                                     GPIO2 ──────────── WS2812B Data In
                                     3.3V / GND ─────── WS2812B GND (shared)
                                                         WS2812B 5V ← PC 5V header
```

> ⚠️ **Important:** WS2812B LEDs run on 5V data logic. The S3-Zero outputs 3.3V.
> Most modern WS2812B strips tolerate this fine. If you see flickering or wrong colours,
> add a 74AHCT125 level shifter between GPIO2 and the LED data line.

> ⚠️ **GPIO19 and GPIO20** are reserved for USB D- and D+ — do not use them for LEDs.

> ℹ️ **GPIO21** is the onboard RGB LED on the S3-Zero. You can use this to test
> by setting `pin: GPIO21` and `num_leds: 1` first.

## Setup

### 1. Clone / download this repo

```bash
git clone https://github.com/YOUR_USERNAME/esphome-usb-lamparray
cd esphome-usb-lamparray
```

### 2. Create your secrets file

```yaml
# secrets.yaml (in the same directory as fan-leds.yaml)
wifi_ssid: "YourNetwork"
wifi_password: "YourPassword"
ota_password: "changeme"
api_password: "changeme"
```

### 3. First flash (USB direct)

The S3-Zero uses USB OTG for TinyUSB, which means the normal ESPHome serial
flash won't work after this firmware is installed — you need to use OTA after the
first flash.

For the **very first flash**, hold the BOOT button, then connect USB to your PC.
The board appears as a serial port. Then flash normally:

```bash
esphome run fan-leds.yaml
```

Or from the Home Assistant ESPHome add-on: add the device, paste the YAML, click Upload.

### 4. OTA updates

After the first flash, all subsequent updates work via Wi-Fi OTA as normal — no
need to touch the BOOT button again.

### 5. Verify in Windows

1. Open **Device Manager** → look for the device under "Human Interface Devices"
   as "USB LampArray" (or your custom product name)
2. Open **Settings → Personalisation → Dynamic Lighting** — your device should
   appear in the list
3. Select an effect and watch your fans light up!

## Configuration Reference

```yaml
usb_lamparray:
  # Required
  num_lamps: 16               # Must match your light component's num_leds
  light_id: fan_leds          # ID of your ESPHome light component

  # Optional
  lamp_array_kind: peripheral # One of: keyboard, mouse, game_controller,
                              #   peripheral, scene, notification,
                              #   chassis, wearable, furniture, art
  manufacturer: "ESPHome"
  product: "PC Fan LampArray"
  autonomous_mode_color: [0, 0, 20]  # [R,G,B] shown when no app is controlling
  vendor_id: 0x303A           # USB Vendor ID (Espressif default)
  product_id: 0x4004          # USB Product ID
```

### lamp_array_kind

Windows uses this hint to decide where to show your device in the Dynamic Lighting
UI and what effects to apply by default:

- `peripheral` — generic external peripheral (good default for fans)
- `chassis` — PC case lighting (also good for fans)
- `keyboard` / `mouse` — will show up in keyboard/mouse sections
- `scene` — ambient / room lighting

## How It Works

The ESP32-S3's USB OTG peripheral (on GPIO19/GPIO20) runs TinyUSB in HID device
mode. The firmware implements the full **HID LampArray** protocol from the USB HID
Usage Tables specification (Section 26, Lighting and Illumination):

| Report ID | Direction | Purpose |
|-----------|-----------|---------|
| 0x01 | Device → Host | Describe array (lamp count, bounding box, kind) |
| 0x02 | Host → Device | Request attributes for a specific lamp |
| 0x03 | Device → Host | Respond with lamp position, colour capabilities |
| 0x04 | Host → Device | Set colours for up to 8 lamps at once |
| 0x05 | Host → Device | Set one colour across a range of lamps |
| 0x06 | Host → Device | Switch between autonomous/host-controlled mode |

Lamp positions are laid out in a circle (radius 50mm) in the XY plane, which
matches the geometry of a fan ring and enables spatially-correct effects like
waves and sweeps.

## Troubleshooting

**Device doesn't appear in Device Manager**
- Make sure `board_build.usb_mode: 1` is set in `platformio_options`
- Check GPIO19/20 are not used for anything else
- Try a different USB cable (some are charge-only)

**Device appears but not in Dynamic Lighting settings**
- Requires Windows 11 Build 23466 or later
- Open Device Manager → right-click the HID device → Properties → check VID/PID

**LEDs flicker or show wrong colours**
- Check `type: GRB` in your light config (WS2812B is GRB, not RGB)
- If data line is unreliable at 3.3V, add a 74AHCT125 level shifter

**OTA stopped working after first flash**
- This shouldn't happen — OTA uses Wi-Fi, not USB
- If Wi-Fi doesn't connect, hold BOOT + RESET to re-enter download mode

**ESPHome compilation errors**
- Make sure `framework: type: esp-idf` is set (not Arduino)
- Requires ESPHome 2024.6.0 or later (for TinyUSB component support)

## Acknowledgements

- [Microsoft ArduinoHidForWindows](https://github.com/microsoft/ArduinoHidForWindows) — reference LampArray implementation and HID descriptor guidance
- [ESP-IDF TinyUSB Device Stack](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_device.html) — USB HID device support on ESP32-S3
- [ESPHome](https://esphome.io) — the best firmware framework for home automation hardware
