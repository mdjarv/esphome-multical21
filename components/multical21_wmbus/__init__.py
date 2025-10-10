"""ESPHome component for Multical21 wMBUS receiver with CC1101 radio."""
import esphome.codegen as cg
from esphome.components import spi

CODEOWNERS = ["@mdjarv"]
multical21_wmbus_ns = cg.esphome_ns.namespace("multical21_wmbus")
Multical21WMBusComponent = multical21_wmbus_ns.class_(
    "Multical21WMBusComponent", cg.PollingComponent, spi.SPIDevice
)
