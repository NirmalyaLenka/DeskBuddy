# Hardware

## Parts

- NodeMCU ESP8266, ESP-12E
- Six-LED addressable RGB ring (WS2812-compatible), three wires: `+`, `GND`, `SIG`
- Regulated 5 V supply for the LEDs
- 330 to 470 ohm resistor, 470 to 1000 uF capacitor
- Optional: 74AHCT125 or 74HCT245 level shifter

## Wiring

| Ring | Connects to |
|---|---|
| SIG | NodeMCU D5 (GPIO14) through a 330 to 470 ohm resistor |
| + | Regulated 5 V supply |
| GND | Supply ground and NodeMCU GND (common ground is required) |

Place the capacitor across the LED supply, close to the ring. Power the NodeMCU from its USB cable.

## Notes

- The NodeMCU outputs 3.3 V logic. Many WS2812-type LEDs on 5 V expect about 3.5 V for a high level. If you see flicker or wrong colors, add a level shifter.
- Six LEDs at full white draw about 360 mA at most. Normal animations use far less.
- Avoid GPIO0, GPIO2 and GPIO15 for the data line; D5 / GPIO14 has no boot restrictions.
- The firmware turns the Wi-Fi radio off. This saves power and avoids timing glitches on the LED data line.

## Calibrating LED order

The boot sweep lights LED 1, then 2, 3, 4, 5, 6, clockwise, over about 1.5 s. Compare with the real ring.

| Observation | Fix in `DeskBuddy.ino` |
|---|---|
| Sweep runs counter-clockwise | `RING_REVERSED = true` |
| Sweep starts at the wrong LED | Set `LED_INDEX_OFFSET` to the pixel (0 to 5) you want as LED 1 |
| Red and green swapped | `LED_STRIP_TYPE` to `NEO_RGB` |
| Colors wrong in another way | Try `NEO_BRG` or `NEO_RBG` |

All animations, the notification direction and the combined-alert split use this mapping.
