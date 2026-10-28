# Testing

Install the test sender dependency and close any program that holds the COM port:

```
pip install -r tools/requirements.txt
python tools/serial_test_sender.py COM5 <scenario>
```

Replace COM5 with your port. Stop with Ctrl+C. The sender waits 3 s after opening the port for the board to reset.

## Scenarios

| Test | Scenario | Expected |
|---|---|---|
| Boot and LED order | Press reset | LEDs fill 1 to 6 clockwise, then the amber link-lost spinner |
| Idle | `idle` | Blue-teal comet circles clockwise, no alerts |
| Music | `music` | Scenes follow the synthetic 120 BPM beat; wait over 30 s to see rotation |
| Temperature alert | `temp` | After about 0.4 s all LEDs breathe red |
| CPU alert | `cpu` | All LEDs breathe pink |
| Combined alert | `both` | LEDs 1, 2, 3 red; LEDs 4, 5, 6 pink |
| Temperature unavailable | `notemp` | Idle spinner only, no red |
| Notification | `notify` | Every 4 s three fast white passes LED 1 to 6, then idle resumes |
| Disconnect and recovery | `disconnect` | Amber spinner after 3 s of silence; idle again when data resumes, no false notification |
| Full sequence | `demo` | All of the above, plus a notification during music and during an alert |

Then repeat with the real PC program: play music, load the CPU, trigger a notification, unplug and replug the USB cable.

## What was verified without hardware

The firmware logic was compiled on a PC against stub Arduino headers, with address and undefined-behaviour sanitizers, and run through a scripted simulation: protocol rejection (old five-value format, extra fields, text, inf, nan), split lines, both alerts and the combined split, unavailable temperature, notification order and single firing, corrupted counter rejection, audio entry and exit, alert priority over audio, link loss and recovery, and 200,000 random garbage inputs.

## Not verified without hardware

- Compilation with the real ESP8266 toolchain and Adafruit NeoPixel timing at 3.3 V data on your ring.
- Physical LED order, direction and color accuracy, including the pink.
- Beat detection and scene feel with the real audio values from the PC program.
- That your board resets when the COM port opens (most do).
