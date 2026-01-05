import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

from . import (
    altherma_hub_ns,
    ALTHERMA_COMMON_SCHEMA,
    altherma_to_code,
)

AlthermaSensor = altherma_hub_ns.class_(
    "AlthermaSensor",
    sensor.Sensor
)

CONFIG_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(AlthermaSensor),
        **ALTHERMA_COMMON_SCHEMA,
    }
)


async def to_code(config):
    sens = cg.new_Pvariable(config[CONF_ID])
    await sensor.register_sensor(sens, config)
    await altherma_to_code(config, sens)