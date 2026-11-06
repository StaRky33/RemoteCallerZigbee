# ESP32-C6 Remote Caller Zigbee

A wireless call system built with **three ESP32-C6 boards** talking over Zigbee. Two emitters (remotes) call a central receiver. The receiver shows who called, sounds an escalating buzzer alarm, and can switch everything off with one press.

## Flash it online

**[Open the web flasher](https://starky33.github.io/RemoteCallerZigbee/)**

1. Plug an ESP32-C6 into your computer over USB.
2. Open the page in **Chrome or Edge** (Web Serial is not available in Firefox or Safari).
3. Click the role you want to flash: **Receiver**, **Emitter 1** or **Emitter 2**.
4. Tick **Erase device** on a first install or if pairing is stuck, then follow the prompts.

The firmware is compiled by GitHub Actions on every push to `main`, so the page always matches the latest code.

> Flash and power the **receiver first**. After a full erase of the receiver, erase the emitters too so they can join the new network.

## Capabilities

- **Zigbee network:** the receiver is the coordinator, the two emitters join as end devices. No hub or internet needed.
- **Emitter identification:** each emitter has its own Zigbee endpoint (10 and 11), so the receiver knows which one called.
- **Per-emitter feedback:** pressing an emitter lights that emitter and the receiver. Emitter 1 is blue, emitter 2 is yellow, and the receiver shows green when both are active.
- **Reset everything:** one press on the receiver turns off the receiver and both emitters and stops the buzzer.
- **Escalating alarm:** when an emitter calls, the receiver's buzzer plays 9 rounds of 2 beeps, 10 s apart, at low, then medium, then high volume.
- **Link status:** a short red flash (200 ms every 3 s) means the board is not connected. On an emitter it shows that it has no network. On the receiver it shows that no device is paired.
- **Adjustable brightness:** two buttons change the LED brightness from 15 to 255 in 7 steps. The LED previews the new level while a button is held, and the value is saved across power cycles.
- **Self-recovery:** an emitter that fails to start Zigbee three times in a row wipes its Zigbee storage and retries clean.

## LED reference

| Color | Emitter | Receiver |
|---|---|---|
| Blue | Emitter 1 is on | Emitter 1 is on |
| Yellow | Emitter 2 is on | Emitter 2 is on |
| Green | Brightness preview (receiver) | Both emitters are on |
| Red flash | Not connected | No device paired |
| Off | Idle | Idle |

## Controls

| Action | Emitter | Receiver |
|---|---|---|
| Short press on BOOT | Send a call to the receiver | Switch everything off and stop the buzzer |
| Hold BOOT for 3 s | Wipe Zigbee storage and reboot (re-pair from scratch) | Zigbee factory reset |
| UP / DOWN buttons | Brightness +/- | Brightness +/- |

## Hardware

- 3 × ESP32-C6 development boards (onboard RGB LED and BOOT button)
- 6 × push buttons (2 per board for brightness; the BOOT button is the main button)
- 1 × **passive** piezo buzzer (receiver only; an active buzzer cannot change volume)

## Pinout

All buttons connect between the GPIO and **GND**. Internal pull-ups are used, so no resistors are needed.

| Function | GPIO | Boards |
|---|---|---|
| Main button | `BOOT_PIN` (GPIO 9, onboard) | All |
| Brightness UP | 19 | All |
| Brightness DOWN | 20 | All |
| Buzzer (passive piezo, other pin to GND) | 18 | Receiver only |
| RGB LED | `RGB_BUILTIN` (onboard) | All |

Avoid the strapping pins (4, 5, 8, 9, 15) for extra hardware.

## Build from source

The whole project is one sketch, `RemoteCallerZigbee.ino`. Change `DEVICE_INDEX` at the top to choose the role:

| `DEVICE_INDEX` | Role | Tools > Zigbee Mode | Tools > Partition Scheme |
|---|---|---|---|
| 1 | Emitter 1 | Zigbee ED (end device) | Zigbee 4MB with spiffs |
| 2 | Emitter 2 | Zigbee ED (end device) | Zigbee 4MB with spiffs |
| 3 | Receiver | Zigbee ZCZR (coordinator/router) | Zigbee ZCZR 4MB with spiffs |

Other settings:

- **Board:** ESP32C6 Dev Module
- **Core:** esp32 by Espressif, version 3.3.12
- **USB CDC On Boot:** Enabled (for the serial monitor)
- **Erase All Flash Before Sketch Upload:** Enabled for the first upload only

If the Zigbee mode does not match `DEVICE_INDEX`, the sketch stops with a compile error.

## Troubleshooting

- **Emitter never pairs:** hold BOOT for 3 s to wipe its Zigbee storage, or flash it again with *Erase device* ticked. The receiver opens its pairing window for 180 s after it boots, so power-cycle the receiver, then the emitter.
- **Bootloop at start:** check that the Zigbee mode and partition scheme match the table above.
- **No serial output:** enable *USB CDC On Boot*.
- **Brightness reset to default:** a full flash erase also clears the saved brightness.

## Future improvements

- **Battery-powered emitters with deep sleep:** the emitters only need to wake on a button press. Ideas are an RTC/EXT wake-up on the BOOT button, rejoining the network quickly after waking, and a low-battery indicator. The receiver stays powered, since the coordinator has to be always on.
- **Power budget:** measure idle and wake current and pick a battery and charging circuit.
- **Pairing window management:** reopen it automatically when an emitter is missing.
- **Faster loss detection on the receiver** when an emitter is powered off.
- **More emitters** and a configurable alarm pattern.
- **Reliability:** acknowledge messages and retry on failure.

## License

MIT — see [`LICENSE`](LICENSE)