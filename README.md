# Zigbee Pool Light Temp
![Version](https://img.shields.io/badge/version-1.0.1-blue)
![ESP32-C6](https://img.shields.io/badge/ESP32-C6-Seeed%20XIAO)
![Zigbee](https://img.shields.io/badge/Zigbee-3.0-green)
![License](https://img.shields.io/badge/License-Apache%202.0-green)

Zigbee-enabled pool light controller with integrated NTC temperature sensor. Controls a 12V AC pool light relay while reporting water temperature to Zigbee2MQTT and Home Assistant.
---

## 🌐 Web Builder

**Build and flash your firmware online!**  
Use the [Zigbee Pool Light Temp Web Builder](https://starky33.github.io/Zigbee-Pool-Light-Temp/) to generate and flash custom firmware directly from your browser — no ESP-IDF installation required.

---

## Features

| Feature | Details |
|---|---|
| Integration | Zigbee2MQTT + Home Assistant |
| Temperature Range | -40°C to +85°C (NTC 10kΩ) |
| Relay Control | On/Off via Zigbee (Endpoint 10) |
| Temperature Reporting | Configurable interval (default 5 min) |
| Calibration | Adjustable offset ±10°C, NTC beta coefficient |
| Antenna | External antenna for improved signal strength |

---

## Bill of Materials

| Component | Reference | Price |
|---|---|---|
| Temperature probe | NTC 10kΩ 035HP05202 | ~€10.00 |
| ESP32-C6 | XIAO ESP32-C6 | ~€5.00 |
| Relay module | 5V 1CH optocoupler | ~€2.00 |
| LM2596HV | Step-down regulator | ~€4.00 |
| **Total** | | **~€21.00** |

> **Note**: The Seeed Studio XIAO ESP32C6 includes a built-in antenna connector and external antenna for better range compared to the standard DevKit board.

---

## Hardware

### How it works

The device uses two Zigbee endpoints:

- **Endpoint 10**: On/Off cluster — Relay control (GPIO6)
- **Endpoint 11**: Temperature measurement — NTC probe (GPIO1 ADC)

The ESP32-C6 reads an NTC thermistor via its ADC (GPIO1), applies calibration offsets, and reports the temperature over Zigbee. The relay coil is driven by GPIO6 to switch the 12V AC pool light circuit.

### Wiring

#### Power supply (LM2596HV)

```
12V AC input ──► LM2596HV IN+/IN-
                 LM2596HV OUT+ ──► XIAO ESP32C6 3V3
                 LM2596HV OUT+ ──► Relay VCC
                 LM2596HV OUT- ──► GND (common)
```

#### Relay (GPIO6)

```
XIAO ESP32C6 GPIO6 ──► Relay IN
Relay VCC          ──► 3.3V
Relay GND          ──► GND
Relay COM          ──► 12V AC (load)
Relay NO           ──► Pool LED 12V AC terminal
```

#### NTC temperature probe (GPIO1)

```
3.3V (XIAO 3V3)
  │
  10kΩ  (1% reference resistor)
  │
  ├──── GPIO1 (ADC1_CH0 on XIAO ESP32C6)
  │
[NTC 10kΩ probe]
  │
GND
```

> ⚠️ **IMPORTANT**: Never exceed 3.3V on the ESP32-C6 ADC pin. The XIAO ESP32C6 operates at 3.3V logic level.

---

## Zigbee endpoints

| Endpoint | Cluster | Role | GPIO |
|---|---|---|---|
| 10 | On/Off (0x0006) | Relay control | GPIO6 |
| 11 | Temperature Measurement (0x0402) | NTC reading | GPIO1 |

---

## Software requirements

| Requirement | Version |
|---|---|
| ESP-IDF | 5.1+ |
| Python | 3.8+ |
| Zigbee2MQTT | 1.36+ |

---

## Build & flash

```bash
# Clone the repository
git clone https://github.com/StaRky33/Zigbee-Pool-Light-Temp.git
cd Zigbee-Pool-Light-Temp

# Set target (ESP32-C6)
idf.py --preview set-target esp32c6

# Erase flash before first use
idf.py -p /dev/ttyUSB0 erase-flash

# Build and flash
idf.py -p /dev/ttyUSB0 flash monitor
```

> **Note**: Replace `/dev/ttyUSB0` with your serial port (e.g., `COM3` on Windows, `/dev/cu.usbserial-*` on macOS).

---

## Configuration

### NTC Beta parameter

The NTC beta coefficient relates the thermistor's resistance to temperature. The default value of 3950 works for most 10kΩ NTC sensors.

For field calibration:
1. Measure resistance at 0°C (ice water) → R0
2. Measure resistance at 100°C (boiling water) → R100
3. `Beta = ln(R0 / R100) / (1/273.15 - 1/373.15)`

### GPIO assignments

| Constant | File | Default | Description |
|---|---|---|---|
| `RELAY_GPIO` | `main/relay.h` | GPIO6 | Relay control (was GPIO4 in v1.0.0) |
| `NTC_GPIO` | `main/ntc.h` | GPIO1 | NTC ADC input |

---

## Zigbee2MQTT integration

### Exposed entities

| Entity | Type | Cluster | Endpoint | Access |
|---|---|---|---|---|
| `state` | Switch (on/off) | genOnOff | 10 | R/W |
| `temperature` | Temperature (°C) | msTemperatureMeasurement | 11 | R |
| `temperature_offset` | Numeric (±10°C) | msTemperatureMeasurement (0xFF00) | 11 | R/W |
| `ntc_beta` | Numeric (3000-5000 K) | msTemperatureMeasurement (0xFF01) | 11 | R/W |
| `fw_version` | Text | genBasic | - | R |

### Setting temperature offset

From Home Assistant or via MQTT:

```yaml
# Add +0.50°C to temperature reading
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"temperature_offset": 0.50}'
```

```yaml
# Subtract -1.00°C from temperature reading
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"temperature_offset": -1.00}'
```

### Setting NTC beta coefficient

```yaml
# Update NTC beta for your specific probe
service: mqtt.publish
data:
  topic: zigbee2mqtt/PoolLightTemp/set
  payload: '{"ntc_beta": 3950}'
```

---

## Project structure

```
Zigbee-Pool-Light-Temp/
├── CMakeLists.txt
├── external_converters/
│   └── poolLightTemp.mjs          # Zigbee2MQTT external converter
├── main/
│   ├── CMakeLists.txt
│   ├── esp_zb_pool.c              # Main Zigbee application
│   ├── esp_zb_pool.h              # Headers & constants
│   ├── ntc.h                      # NTC temperature sensor
│   ├── relay.h                    # Relay control
│   └── version.h                  # Firmware version
└── manifest.json                  # ESP-IDF manifest
```

---

## Changelog

### v1.0.1 (2025-06-11)
- **Hardware**: Replaced ESP32-C6 DevKit with Seeed Studio XIAO ESP32C6 (with external antenna)
- **GPIO**: Changed relay control from GPIO4 to GPIO6
- **Firmware**: Added report_interval attribute (0xFF02) for configurable reporting
- **Converter**: Added graceful error handling for unsupported attributes

### v1.0.0 (2025-XX-XX)
- Initial release
- ESP32-C6 DevKit board
- GPIO4 for relay control

---

## License

Apache License 2.0 — see [LICENSE](LICENSE).

---

## Support

For issues and questions:
- GitHub Issues: https://github.com/starkydiy/Zigbee-Pool-Light-Temp/issues
- Zigbee2MQTT Community: https://community.zigbee2mqtt.io/