"""Grain-silo application profile; deliberately outside AutoLink wire SSOT."""

PROFILE = {
    'max_nodes': 8,
    'test_network_id': 0xA5A5A5A5A5A5A5A5,
    'local_sensor_drivers': {'CO2': 0x02, 'MOISTURE': 0x03},
    'local_channels': {'CO2': 0x03, 'MOISTURE': 0x04},
}
