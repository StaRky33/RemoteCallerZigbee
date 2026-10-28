# ESP32-C6 Remote Caller Zigbee

A simple wireless remote system built with **three ESP32-C6 boards** communicating over Zigbee. Two remotes send signals to a central receiver, which acknowledges them through its onboard LED.

The project is developed using **Arduino IDE**.

## Hardware

- 3 × ESP32-C6 development boards
- 3 × Push buttons (one per board)
- Onboard LED on each ESP32-C6

## System Overview

The system consists of:
- **Remote A:** Green status LED
- **Remote B:** Yellow status LED
- **Receiver:** Receives remote signals and acknowledges them

The two remotes automatically connect to the receiver over Zigbee.

## Behavior

### Remotes

- Each remote has a unique color:
  - Remote A: Green
  - Remote B: Yellow
- Pressing a remote's button sends a Zigbee message to the receiver.
- The remote's LED indicates its current status.
- When the receiver acknowledges the signal, the remote's LED turns blue.

### Receiver

- The receiver automatically accepts connections from both remotes.
- When Remote A is pressed, the receiver's LED turns green.
- When Remote B is pressed, the receiver's LED turns yellow.
- When the receiver's button is pressed:
  - The receiver's LED turns blue for 1 minute.
  - Any remote that was pressed before the acknowledgement also turns blue.
- After 1 minute, the receiver's LED returns to its idle state.

## Communication

The devices communicate using **Zigbee**, with the receiver acting as the central coordinator and the two remotes joining its network.

## Development Environment

- **IDE:** Arduino IDE
- **Microcontroller:** ESP32-C6
- **Wireless protocol:** Zigbee

## Project Goals

- Simple, reliable wireless communication
- Automatic joining of remotes to the receiver
- Clear visual feedback using onboard LEDs
- Acknowledgement of remote signals
- Minimal hardware and wiring

## Future Improvements

- Add support for more remotes.
- Improve handling of simultaneous remote signals.
- Add configurable acknowledgement timing.
- Improve reconnection and message reliability.

## License

MIT — see [`LICENSE`](LICENSE)

---