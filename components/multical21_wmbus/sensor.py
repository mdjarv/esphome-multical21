"""ESPHome component for Multical21 wMBUS receiver with CC1101 radio."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, spi
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_WATER,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_TOTAL_INCREASING,
    STATE_CLASS_MEASUREMENT,
    UNIT_CUBIC_METER,
    UNIT_CELSIUS,
    ICON_WATER,
    ICON_THERMOMETER,
)
from . import multical21_wmbus_ns, Multical21WMBusComponent

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_METER_ID = "meter_id"
CONF_AES_KEY = "aes_key"
CONF_GDO0_PIN = "gdo0_pin"
CONF_TOTAL_CONSUMPTION = "total_consumption"
CONF_TARGET_CONSUMPTION = "target_consumption"
CONF_FLOW_TEMPERATURE = "flow_temperature"
CONF_AMBIENT_TEMPERATURE = "ambient_temperature"

def validate_aes_key(value):
    """Validate AES key is 16 bytes (32 hex characters)."""
    if isinstance(value, str):
        value = value.replace(" ", "").replace(":", "")
        if len(value) != 32:
            raise cv.Invalid("AES key must be exactly 32 hexadecimal characters (16 bytes)")
        try:
            bytes.fromhex(value)
        except ValueError as e:
            raise cv.Invalid(f"Invalid hexadecimal characters in AES key: {e}")
    return value

def validate_meter_id(value):
    """Validate meter ID is 4 bytes (8 hex characters)."""
    if isinstance(value, str):
        value = value.replace(" ", "").replace(":", "")
        if len(value) != 8:
            raise cv.Invalid("Meter ID must be exactly 8 hexadecimal characters (4 bytes)")
        try:
            bytes.fromhex(value)
        except ValueError as e:
            raise cv.Invalid(f"Invalid hexadecimal characters in meter ID: {e}")
    return value

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Multical21WMBusComponent),
            cv.Required(CONF_METER_ID): validate_meter_id,
            cv.Required(CONF_AES_KEY): validate_aes_key,
            cv.Required(CONF_GDO0_PIN): cv.int_,
            cv.Optional(CONF_TOTAL_CONSUMPTION): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER,
                icon=ICON_WATER,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_WATER,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_TARGET_CONSUMPTION): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER,
                icon=ICON_WATER,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_WATER,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_FLOW_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AMBIENT_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    """Generate C++ code from config."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    # Set meter ID (4 bytes)
    meter_id_str = config[CONF_METER_ID].replace(" ", "").replace(":", "")
    meter_id_bytes = bytes.fromhex(meter_id_str)
    meter_id_array = [int(b) for b in meter_id_bytes]
    cg.add(var.set_meter_id(meter_id_array))

    # Set AES key (16 bytes)
    aes_key_str = config[CONF_AES_KEY].replace(" ", "").replace(":", "")
    aes_key_bytes = bytes.fromhex(aes_key_str)
    aes_key_array = [int(b) for b in aes_key_bytes]
    cg.add(var.set_aes_key(aes_key_array))

    # Set GDO0 pin
    cg.add(var.set_gdo0_pin(config[CONF_GDO0_PIN]))

    # Register sensors
    if CONF_TOTAL_CONSUMPTION in config:
        sens = await sensor.new_sensor(config[CONF_TOTAL_CONSUMPTION])
        cg.add(var.set_total_consumption_sensor(sens))

    if CONF_TARGET_CONSUMPTION in config:
        sens = await sensor.new_sensor(config[CONF_TARGET_CONSUMPTION])
        cg.add(var.set_target_consumption_sensor(sens))

    if CONF_FLOW_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_FLOW_TEMPERATURE])
        cg.add(var.set_flow_temperature_sensor(sens))

    if CONF_AMBIENT_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_AMBIENT_TEMPERATURE])
        cg.add(var.set_ambient_temperature_sensor(sens))
