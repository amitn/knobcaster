"""ESPHome wrapper around the shared Cast stack (../../../components/cast).

The vanilla firmware (main) and this ESPHome target build the SAME
components/cast/ ESP-IDF component — single source of truth, no duplication.
This component pulls it into the ESPHome build and exposes Cast state as HA
entities (devices found, now playing, volume).
"""
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import sensor, text_sensor
from esphome.components.esp32 import add_idf_component
from esphome.components.sh8601 import SH8601
from esphome.components.drv2605 import DRV2605

CONF_DISPLAY = "display"
CONF_HAPTICS = "haptics"
CONF_ENCODER = "encoder"

# How the physical knob is decoded:
#   "pcnt"     — internally, via PCNT (Waveshare board, see cast_controller.cpp).
#   "external" — the YAML feeds detents in via on_encoder_delta() (e.g. ESPHome's
#                stock rotary_encoder on the Elecrow board, a normal quadrature
#                encoder PCNT-special-casing would mis-decode).
ENCODER_MODES = ("pcnt", "external")

AUTO_LOAD = ["sensor", "text_sensor"]

cast_ns = cg.esphome_ns.namespace("cast_controller")
CastController = cast_ns.class_("CastController", cg.Component)

CONF_DEVICES_FOUND = "devices_found"
CONF_NOW_PLAYING = "now_playing"
CONF_CURRENT_DEVICE = "current_device"
CONF_ART_URL = "art_url"
CONF_VOLUME = "volume"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CastController),
        cv.Optional(CONF_DEVICES_FOUND): sensor.sensor_schema(accuracy_decimals=0),
        cv.Optional(CONF_NOW_PLAYING): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_CURRENT_DEVICE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_ART_URL): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_VOLUME): sensor.sensor_schema(
            unit_of_measurement="%", accuracy_decimals=0
        ),
        cv.Optional(CONF_DISPLAY): cv.use_id(SH8601),  # for the 'S' screenshot key
        cv.Optional(CONF_HAPTICS): cv.use_id(DRV2605),  # click per physical detent/press
        cv.Optional(CONF_ENCODER, default="pcnt"): cv.one_of(
            *ENCODER_MODES, lower=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

# Repo root (this file is esphome/components/cast_controller/__init__.py).
_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


async def to_code(config):
    # Build components/cast/ verbatim as a local IDF component (the shared core).
    # Its own idf_component.yml pulls cJSON.
    add_idf_component(name="cast", path=os.path.join(_ROOT, "components", "cast"))
    cg.add_build_flag("-I" + os.path.join(_ROOT, "components", "cast", "include"))
    # board_pins.h (encoder/button GPIOs) is shared with the vanilla bsp.
    cg.add_build_flag("-I" + os.path.join(_ROOT, "components", "bsp", "include"))

    # Compile-optional peripherals: only the board(s) that wire them up pull the
    # SH8601/DRV2605/PCNT code in. The matching #ifdefs in cast_controller.{h,cpp}
    # keep boards without them (e.g. the Elecrow RGB board) linking cleanly.
    if config[CONF_ENCODER] == "pcnt":
        cg.add_build_flag("-DCAST_HAVE_PCNT_ENCODER")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_DISPLAY in config:
        cg.add_build_flag("-DCAST_HAVE_SH8601")
        cg.add(var.set_display(await cg.get_variable(config[CONF_DISPLAY])))
    if CONF_HAPTICS in config:
        cg.add_build_flag("-DCAST_HAVE_HAPTICS")
        cg.add(var.set_haptics(await cg.get_variable(config[CONF_HAPTICS])))

    if CONF_DEVICES_FOUND in config:
        s = await sensor.new_sensor(config[CONF_DEVICES_FOUND])
        cg.add(var.set_devices_found_sensor(s))
    if CONF_VOLUME in config:
        s = await sensor.new_sensor(config[CONF_VOLUME])
        cg.add(var.set_volume_sensor(s))
    if CONF_NOW_PLAYING in config:
        ts = await text_sensor.new_text_sensor(config[CONF_NOW_PLAYING])
        cg.add(var.set_now_playing_sensor(ts))
    if CONF_CURRENT_DEVICE in config:
        ts = await text_sensor.new_text_sensor(config[CONF_CURRENT_DEVICE])
        cg.add(var.set_current_device_sensor(ts))
    if CONF_ART_URL in config:
        ts = await text_sensor.new_text_sensor(config[CONF_ART_URL])
        cg.add(var.set_art_url_sensor(ts))
