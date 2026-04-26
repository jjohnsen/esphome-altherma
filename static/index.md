---
layout: default
title: ESPHome Altherma
---

Built for supported ESP32 boards, ESPHome Altherma monitors Daikin Altherma heat pumps through the X10A connector and integrates directly with Home Assistant on your local network. It exposes temperatures, voltages, currents, flow data, and diagnostics without MQTT or cloud services.

![m5stack Basic running ESPHome Altherma](/assets/images/esphome-altherma-m5stack.jpg)

For hardware requirements, wiring, and advanced configuration, see the <a href="https://github.com/jjohnsen/esphome-altherma">full project docs</a>.

## Install

Connect a supported ESP32 board with USB, and click the connect button below:

<esp-web-install-button manifest="firmware/esphome-altherma.manifest.json"></esp-web-install-button>

## Steps

1. Connect: Plug in the board with USB and allow serial access in the browser.
2. Flash: Click the install button above, choose the serial port, and confirm flashing.
3. Wi-Fi: Follow the prompts to configure your Wi-Fi credentials.
4. Home Assistant: Add the discovered ESPHome device under Settings -> Devices & Services.
5. Emoncms (optional): For long-term COP and performance tracking, follow the <a href="https://jjohnsen.no/2026/esphome-altherma-emoncms-setup-guide/">ESPHome Altherma + Emoncms setup guide</a>.

After initial setup, over-the-air updates are managed directly from the Home Assistant UI.

<script data-goatcounter="https://jjohnsen.goatcounter.com/count"
        async src="//gc.zgo.at/count.js"></script>

<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>