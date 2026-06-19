"""DRV2605 haptic driver as an ESPHome i2c component.

ESPHome has no built-in DRV2605, so this is a small custom component. The effect
default (24 = "Sharp Tick") is the value tuned on hardware for the vanilla build
(components/bsp/haptics.c). Call id(...).play() from automations (e.g. per
encoder detent) for a click.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c"]

drv2605_ns = cg.esphome_ns.namespace("drv2605")
DRV2605 = drv2605_ns.class_("DRV2605", cg.Component, i2c.I2CDevice)

CONF_EFFECT = "effect"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DRV2605),
            cv.Optional(CONF_EFFECT, default=24): cv.int_range(min=1, max=123),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x5A))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_effect(config[CONF_EFFECT]))
