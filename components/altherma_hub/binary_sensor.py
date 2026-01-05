import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from . import (
    altherma_hub_ns,
    ALTHERMA_COMMON_SCHEMA,
    altherma_to_code,
)

AlthermaBinarySensor = altherma_hub_ns.class_(
    "AlthermaBinarySensor",
    binary_sensor.BinarySensor
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(AlthermaBinarySensor),
        **ALTHERMA_COMMON_SCHEMA,
    }
)


async def to_code(config):
    sens = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(sens, config)
    await altherma_to_code(config, sens)