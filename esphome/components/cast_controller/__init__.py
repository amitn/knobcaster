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

AUTO_LOAD = ["sensor", "text_sensor"]

cast_ns = cg.esphome_ns.namespace("cast_controller")
CastController = cast_ns.class_("CastController", cg.Component)

CONF_DEVICES_FOUND = "devices_found"
CONF_NOW_PLAYING = "now_playing"
CONF_VOLUME = "volume"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CastController),
        cv.Optional(CONF_DEVICES_FOUND): sensor.sensor_schema(accuracy_decimals=0),
        cv.Optional(CONF_NOW_PLAYING): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_VOLUME): sensor.sensor_schema(
            unit_of_measurement="%", accuracy_decimals=0
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

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_DEVICES_FOUND in config:
        s = await sensor.new_sensor(config[CONF_DEVICES_FOUND])
        cg.add(var.set_devices_found_sensor(s))
    if CONF_VOLUME in config:
        s = await sensor.new_sensor(config[CONF_VOLUME])
        cg.add(var.set_volume_sensor(s))
    if CONF_NOW_PLAYING in config:
        ts = await text_sensor.new_text_sensor(config[CONF_NOW_PLAYING])
        cg.add(var.set_now_playing_sensor(ts))
