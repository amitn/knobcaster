"""ESPHome `display` platform for the SH8601 QSPI AMOLED.

ESPHome's built-in qspi_dbi/mipi_spi does NOT frame SH8601's QSPI commands
correctly (0x02,0x00,<cmd>,0x00,<data>), so a CUSTOM init_sequence renders
nothing (esphome discussion #3229). This wraps the espressif/esp_lcd_sh8601 IDF
driver — the same one the vanilla firmware uses (components/bsp/display.c) —
and reuses the shared init table (components/bsp/include/sh8601_init_cmds.h).
"""
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display
from esphome.components.esp32 import add_idf_component
from esphome.const import (
    CONF_CS_PIN,
    CONF_DIMENSIONS,
    CONF_HEIGHT,
    CONF_ID,
    CONF_RESET_PIN,
    CONF_WIDTH,
)

from . import SH8601

DEPENDENCIES = ["esp32"]

CONF_CLK_PIN = "clk_pin"
CONF_DATA_PINS = "data_pins"

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(SH8601),
        cv.Required(CONF_CS_PIN): cv.int_,
        cv.Required(CONF_RESET_PIN): cv.int_,
        cv.Required(CONF_CLK_PIN): cv.int_,
        cv.Required(CONF_DATA_PINS): cv.All([cv.int_], cv.Length(min=4, max=4)),
        cv.Optional(
            CONF_DIMENSIONS, default={"width": 360, "height": 360}
        ): cv.Schema(
            {cv.Required(CONF_WIDTH): cv.int_, cv.Required(CONF_HEIGHT): cv.int_}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


async def to_code(config):
    # The SH8601 IDF driver (handles QSPI command framing) + the shared init table.
    add_idf_component(name="espressif/esp_lcd_sh8601", ref="1.0.0")
    cg.add_build_flag("-I" + os.path.join(_ROOT, "components", "bsp", "include"))

    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)  # also registers the Component

    d = config[CONF_DATA_PINS]
    cg.add(
        var.set_pins(
            config[CONF_CS_PIN], config[CONF_RESET_PIN], config[CONF_CLK_PIN],
            d[0], d[1], d[2], d[3],
        )
    )
    cg.add(
        var.set_dimensions(
            config[CONF_DIMENSIONS][CONF_WIDTH], config[CONF_DIMENSIONS][CONF_HEIGHT]
        )
    )
