"""ESPHome wrapper around the shared Cast stack (../../../components/cast).

The vanilla firmware (main) and this ESPHome target build the SAME
components/cast/ ESP-IDF component — single source of truth, no duplication.
This component pulls it (and its cJSON dep) into the ESPHome build and exposes
it as an ESPHome Component.
"""
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components.esp32 import add_idf_component

cast_ns = cg.esphome_ns.namespace("cast_controller")
CastController = cast_ns.class_("CastController", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CastController),
    }
).extend(cv.COMPONENT_SCHEMA)

# Repo root (this file is esphome/components/cast_controller/__init__.py).
_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


async def to_code(config):
    # Build components/cast/ verbatim as a local IDF component (the shared core).
    # Its own idf_component.yml pulls cJSON, so we don't add it separately here.
    add_idf_component(name="cast", path=os.path.join(_ROOT, "components", "cast"))
    cg.add_build_flag("-I" + os.path.join(_ROOT, "components", "cast", "include"))

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
