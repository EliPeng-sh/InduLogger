# InduLogger

> Product-grade STM32 + FreeRTOS industrial data-logging platform with an ESP32-S3
> connectivity co-processor (Wi-Fi / BLE / MQTT) and secure FOTA with power-fail
> recovery and automatic rollback.

**Status**: 🚧 In development — see [`docs/specs/`](docs/specs/) for the design document.

## Planned highlights

- STM32F411 + FreeRTOS: sensor sampling, SD-card logging (FATFS), Modbus RTU slave
- ESP32-S3 co-processor: Wi-Fi + MQTT, BLE provisioning, custom UART protocol (CRC16, ACK/retry)
- v2: Secure FOTA — dual-slot bootloader, SHA256 verification, automatic rollback
- Layered "platform" architecture, extensible to instrumentation and medical verticals
- **Measured, not claimed**: interrupt latency & jitter under stress, power-cut torture
  tests, and 7-day soak results — all published with methodology in `docs/validation/`

Full documentation, wiring diagrams and demo videos will land here as milestones complete.
