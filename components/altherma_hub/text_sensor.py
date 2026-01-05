import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

from . import (
    altherma_hub_ns,
    ALTHERMA_COMMON_SCHEMA,
    altherma_to_code,
)

AlthermaTextSensor = altherma_hub_ns.class_(
    "AlthermaTextSensor", 
    text_sensor.TextSensor
)

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(AlthermaTextSensor),
        **ALTHERMA_COMMON_SCHEMA,
    }
)


async def to_code(config):
    sens = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(sens, config)
    await altherma_to_code(config, sens)