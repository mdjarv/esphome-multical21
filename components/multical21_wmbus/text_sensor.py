"""Text sensor support for Multical21 wMBUS receiver."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from . import multical21_wmbus_ns, Multical21WMBusComponent

CONF_MULTICAL21_WMBUS_ID = "multical21_wmbus_id"
CONF_INFO_CODES = "info_codes"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MULTICAL21_WMBUS_ID): cv.use_id(Multical21WMBusComponent),
        cv.Optional(CONF_INFO_CODES): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle",
        ),
    }
)


async def to_code(config):
    """Generate C++ code from config."""
    parent = await cg.get_variable(config[CONF_MULTICAL21_WMBUS_ID])

    if CONF_INFO_CODES in config:
        sens = await text_sensor.new_text_sensor(config[CONF_INFO_CODES])
        cg.add(parent.set_info_codes_sensor(sens))
