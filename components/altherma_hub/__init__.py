import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import uart, text_sensor
import os
from pathlib import Path

DEPENDENCIES = ["uart", "api"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]
MULTI_CONF = True

altherma_hub_ns = cg.esphome_ns.namespace("altherma_hub")
AlthermaHub = altherma_hub_ns.class_(
    "AlthermaHub",
    cg.PollingComponent,
    uart.UARTDevice,
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlthermaHub),
            cv.Optional("query_result_text_sensor"): cv.use_id(text_sensor.TextSensor),
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if "query_result_text_sensor" in config:
        query_sensor = await cg.get_variable(config["query_result_text_sensor"])
        cg.add(var.set_query_result_text_sensor(query_sensor))

    # Get the absolute path to the lib directory
    lib_path = Path(__file__).parent / "lib"
    
    # Add as extra script to copy files
    cg.add_platformio_option("build_flags", [f"-I{lib_path.as_posix()}"])


# Shared configuration constants
CONF_HUB_ID = "altherma_hub_id"
CONF_REGISTER = "register"
CONF_OFFSET = "offset"
CONF_CONVID = "convid"
CONF_DATASIZE = "datasize"

# Common schema for all Altherma sensors
ALTHERMA_COMMON_SCHEMA = {
    cv.Required(CONF_HUB_ID): cv.use_id(AlthermaHub),
    cv.Required(CONF_REGISTER): cv.All(cv.hex_int, cv.Range(min=0, max=0xFF)),
    cv.Required(CONF_OFFSET): cv.int_,
    cv.Required(CONF_CONVID): cv.int_,
    cv.Required(CONF_DATASIZE): cv.int_,
}


async def altherma_to_code(config, sens):
    """Common code generator for all Altherma sensor types."""
    cg.add(sens.set_registry_id(config[CONF_REGISTER]))
    cg.add(sens.set_offset(config[CONF_OFFSET]))
    cg.add(sens.set_convid(config[CONF_CONVID]))
    cg.add(sens.set_datasize(config[CONF_DATASIZE]))
    
    hub = await cg.get_variable(config[CONF_HUB_ID])
    cg.add(hub.register_sensor(sens))