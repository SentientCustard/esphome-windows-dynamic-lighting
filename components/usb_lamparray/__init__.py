"""ESPHome external component: USB HID LampArray for Windows Dynamic Lighting.

Targets : ESP32-S3 (requires USB OTG peripheral + TinyUSB)
Tested  : Waveshare ESP32-S3-Zero

NOTE: This component owns the tinyusb_driver_install() call entirely.
ESPHome's built-in `tinyusb` component MUST NOT appear in the YAML because:
  1. It uses a nested-struct API (.descriptor = ...) that omits
     configuration_descriptor, causing a fatal assert when CFG_TUD_HID > 0.
  2. tinyusb_driver_install() has no double-install guard — a second call
     unconditionally wipes our HID descriptors.
We pull in the managed component (and its headers) ourselves via
add_idf_component(), which is exactly the same call ESPHome's tinyusb
__init__.py uses. This gets the build system to download the component and
add its headers to the include path without instantiating the broken wrapper.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
from esphome.const import CONF_ID

# "tinyusb" intentionally NOT listed — we call add_idf_component() ourselves.
DEPENDENCIES = ["esp32"]
AUTO_LOAD   = []
CODEOWNERS  = ["@SentientCustard"]

# Namespace / class wiring
usb_lamparray_ns = cg.esphome_ns.namespace("usb_lamparray")
USBLampArrayComponent = usb_lamparray_ns.class_(
    "USBLampArrayComponent", cg.Component
)

# Valid lamp_array_kind strings → numeric values (HID Usage Tables 1.3 §26)
LAMP_ARRAY_KINDS = {
    "undefined":        0x00,
    "keyboard":         0x01,
    "mouse":            0x02,
    "game_controller":  0x03,
    "peripheral":       0x04,
    "scene":            0x05,
    "notification":     0x06,
    "chassis":          0x07,
    "wearable":         0x08,
    "furniture":        0x09,
    "art":              0x0A,
}

# YAML config keys
CONF_NUM_LAMPS        = "num_lamps"
CONF_LAMP_ARRAY_KIND  = "lamp_array_kind"
CONF_LIGHT_ID         = "light_id"
CONF_VENDOR_ID        = "vendor_id"
CONF_PRODUCT_ID       = "product_id"
CONF_MANUFACTURER     = "manufacturer"
CONF_PRODUCT          = "product"
CONF_AUTONOMOUS_COLOR = "autonomous_mode_color"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(USBLampArrayComponent),
        cv.Required(CONF_NUM_LAMPS): cv.int_range(min=1, max=256),
        cv.Optional(CONF_LAMP_ARRAY_KIND, default="peripheral"): cv.enum(
            LAMP_ARRAY_KINDS, lower=True
        ),
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_VENDOR_ID,    default=0x303A): cv.hex_int,
        cv.Optional(CONF_PRODUCT_ID,   default=0x4004): cv.hex_int,
        cv.Optional(CONF_MANUFACTURER, default="ESPHome"): cv.string,
        cv.Optional(CONF_PRODUCT,      default="USB LampArray"): cv.string,
        cv.Optional(CONF_AUTONOMOUS_COLOR, default=[0, 0, 20]): cv.All(
            cv.ensure_list(cv.uint8_t), cv.Length(min=3, max=3)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Pull in espressif/esp_tinyusb managed component directly.
    # This is identical to what ESPHome's tinyusb __init__.py does, so the
    # build system downloads the component and adds its headers to the include
    # path — but WITHOUT instantiating ESPHome's TinyUSB wrapper class or
    # calling tinyusb_driver_install() with the wrong arguments.
    # ref="1.7.6~1" matches what ESPHome's own tinyusb component uses.
    add_idf_component(name="espressif/esp_tinyusb", ref="1.7.6~1")

    # Enable TinyUSB via IDF sdkconfig — NOT via the YAML sdkconfig_options
    # block, which triggers ESPHome's tinyusb component auto-loader and causes
    # a second tinyusb_driver_install call with a blank descriptor.
    add_idf_sdkconfig_option("CONFIG_TINYUSB_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_HID_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_HID_COUNT", 1)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_CDC_ENABLED", False)

    cg.add(var.set_num_lamps(config[CONF_NUM_LAMPS]))
    cg.add(var.set_lamp_array_kind(LAMP_ARRAY_KINDS[config[CONF_LAMP_ARRAY_KIND]]))

    light_var = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(light_var))

    cg.add(var.set_vendor_id(config[CONF_VENDOR_ID]))
    cg.add(var.set_product_id(config[CONF_PRODUCT_ID]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_product(config[CONF_PRODUCT]))

    r, g, b = config[CONF_AUTONOMOUS_COLOR]
    cg.add(var.set_autonomous_mode_color(r, g, b))
