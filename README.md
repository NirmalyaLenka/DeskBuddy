# Desk Buddy

ESP8266 firmware for a desk companion built from a NodeMCU and a ring of six addressable RGB LEDs. A Windows PC program sends audio levels, CPU load, CPU temperature and notification events over USB serial. The ring shows them as animations.

[![ESP8266](https://img.shields.io/badge/Controller-ESP8266-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/) [![Arduino](https://img.shields.io/badge/Firmware-Arduino%20C%2B%2B-00979D?logo=arduino&logoColor=white)](https://www.arduino.cc/) [![Python](https://img.shields.io/badge/PC%20Bridge-Python-3776AB?logo=python&logoColor=white)](https://www.python.org/) [![NeoPixel](https://img.shields.io/badge/LEDs-Addressable%20RGB-7C3AED)](https://learn.adafruit.com/adafruit-neopixel-uberguide)

## What it does

- Idle: a smooth clockwise loading spinner.
- Music: five audio-reactive scenes (orbit, shockwave, spectrum, meter, sparkle) driven by overall, bass, mid and treble levels.
- CPU temperature above 80 C: all LEDs glow red.
- CPU utilization above 80 percent: all LEDs glow pink.
- Both at once: LEDs 1 to 3 red, LEDs 4 to 6 pink.
- Notification: three fast white passes from LED 1 to LED 6, then the previous mode resumes.
- PC unplugged or silent for 3 s: dim amber spinner, automatic recovery when data returns.

## Repository layout

```
firmware/DeskBuddy/DeskBuddy.ino   Arduino sketch (folder name must match)
tools/serial_test_sender.py        Synthetic data sender for testing without the PC program
tools/requirements.txt             Python dependency for the test sender
docs/protocol.md                   Serial protocol and what the PC program must send
docs/hardware.md                   Wiring, power and LED order calibration
docs/behaviour.md                  Priority order and every animation
docs/tuning.md                     Adjustable settings
docs/testing.md                    Test procedure and known limits
```

## Quick start

1. Install Arduino IDE 2.x and the board package "esp8266 by ESP8266 Community".
2. Install the library "Adafruit NeoPixel" from the Library Manager.
3. Wire the ring as described in `docs/hardware.md` (data on D5 / GPIO14).
4. Open `firmware/DeskBuddy/DeskBuddy.ino`, select board "NodeMCU 1.0 (ESP-12E Module)", and upload.
5. Watch the boot sweep: LEDs must light 1 to 6 clockwise. If not, see the calibration section in `docs/hardware.md`.
6. Run your PC program, or use the test sender:

```
pip install -r tools/requirements.txt
python tools/serial_test_sender.py COM5 demo
```

Only one program can hold the COM port at a time. Close the Arduino Serial Monitor first.

## Status

Compiled and exercised on a PC against stub Arduino headers, including malformed-input fuzzing. The ESP8266 build and the physical behaviour on real hardware depend on your setup; see `docs/testing.md` for what to verify.

## License

MIT. See `LICENSE`.


---

## Maintenance & Audit

- **Audit date:** 11 November 2026
- **Documentation status:** README reviewed and audit record added on this date.
- **Scope and evidence:** This is a documentation-level maintenance pass. It does not establish that source code compiles, dependencies are current, security has been tested, or runtime behavior has been verified.
- **Code and tests:** No new functional code changes or test results are claimed by this README update. Run project-specific build, test, and security checks before relying on the project.
- **Maintenance baseline:** Documentation status recorded through 11 November 2026; revisit setup instructions, dependencies, security, known limitations, and tests when the project changes.
