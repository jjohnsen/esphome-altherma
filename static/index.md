---
layout: default
title: ESPHome Altherma
---
An [ESPHome](https://esphome.io/) custom component for monitoring [Daikin Altherma](https://www.daikin.eu/) heat pumps via the X10A connector. Exposes temperatures, voltages, currents, and diagnostics directly to [Home Assistant](https://www.home-assistant.io/).

For hardware requirements, wiring diagrams, and full documentation, see the <a href="https://github.com/jjohnsen/esphome-altherma">GitHub repository</a>.

## Supported Boards

- ESP32 DevKit
- ESP32-S3 DevKit
- M5Stack AtomS3 Lite

## Browser Install

Flash the firmware directly from your browser — no tools required.

1. Connect your ESP32 board to your computer via USB
2. Click the **Connect** button below and select the serial port
3. Follow the prompts to install the firmware and configure Wi-Fi

<esp-web-install-button manifest="firmware/esphome-altherma.manifest.json"></esp-web-install-button>

> **Note:** Browser installation requires a Chromium-based browser (Chrome, Edge, etc.) with Web Serial support.

## After Installation

1. The device will appear in Home Assistant under **Settings → Devices & Services** as a discovered ESPHome device
2. Click **Configure** to add it
3. All sensors will be available as Home Assistant entities

Over-the-Air (OTA) Updates are managed directly within the Home Assistant UI.

<script data-goatcounter="https://jjohnsen.goatcounter.com/count"
        async src="//gc.zgo.at/count.js"></script>

<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>