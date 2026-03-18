"""ESPHome external component: USB HID LampArray for Windows Dynamic Lighting.

Targets: ESP32-S3 (requires USB OTG peripheral + TinyUSB)
Tested on: Waveshare ESP32-S3-Zero
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "tinyusb"]
AUTO_LOAD = []
CODEOWNERS = ["@you"]

# Namespace / class wiring
usb_lamparray_ns = cg.esphome_ns.namespace("usb_lamparray")
USBLampArrayComponent = usb_lamparray_ns.class_(
    "USBLampArrayComponent", cg.Component
)

# Valid lamp_array_kind strings → numeric values from the HID spec
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
CONF_NUM_LAMPS          = "num_lamps"
CONF_LAMP_ARRAY_KIND    = "lamp_array_kind"
CONF_LIGHT_ID           = "light_id"
CONF_VENDOR_ID          = "vendor_id"
CONF_PRODUCT_ID         = "product_id"
CONF_MANUFACTURER       = "manufacturer"
CONF_PRODUCT            = "product"
CONF_AUTONOMOUS_COLOR   = "autonomous_mode_color"

# Schema
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

    cg.add(var.set_num_lamps(config[CONF_NUM_LAMPS]))
    cg.add(var.set_lamp_array_kind(LAMP_ARRAY_KINDS[config[CONF_LAMP_ARRAY_KIND]]))

    # Resolve the light reference to its underlying AddressableLight
    light_var = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(light_var.get_output()))

    cg.add(var.set_vendor_id(config[CONF_VENDOR_ID]))
    cg.add(var.set_product_id(config[CONF_PRODUCT_ID]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_product(config[CONF_PRODUCT]))

    r, g, b = config[CONF_AUTONOMOUS_COLOR]
    cg.add(var.set_autonomous_mode_color(r, g, b))

    # TinyUSB include paths — point directly into the PlatformIO ESP-IDF package
    cg.add_build_flag("-DCFG_TUSB_MCU=OPT_MCU_ESP32S3")
    cg.add_build_flag("-DCFG_TUD_HID=1")
    cg.add_build_flag("-DCFG_TUD_HID_EP_BUFSIZE=64")
    cg.add_build_flag("-DCFG_TUD_ENDPOINT0_SIZE=64")
    cg.add_build_flag("-DCFG_TUSB_OS=OPT_OS_FREERTOS")
