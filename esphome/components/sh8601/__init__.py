"""SH8601 QSPI AMOLED display component (the platform lives in display.py)."""
import esphome.codegen as cg
from esphome.components import display

CODEOWNERS = ["@amitn"]

sh8601_ns = cg.esphome_ns.namespace("sh8601")
SH8601 = sh8601_ns.class_("SH8601", cg.Component, display.Display)
