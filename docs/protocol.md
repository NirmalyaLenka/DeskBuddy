# Serial protocol

115200 baud, 8N1, one packet per line, ended by `\n` or `\r\n`.

```
A,audio,bass,mid,treble,cpu,temp,notification_sequence
```

Example: `A,120,230,90,60,35,52.5,4`

## Fields

| Field | Meaning | Accepted | Handling |
|---|---|---|---|
| audio | Overall audio activity | 0 to 300 | Clamped to 255 |
| bass | Low-frequency activity | 0 to 300 | Clamped to 255 |
| mid | Mid-frequency activity | 0 to 300 | Clamped to 255 |
| treble | High-frequency activity | 0 to 300 | Clamped to 255 |
| cpu | CPU utilization percent | 0 to 101 | Clamped to 100 |
| temp | CPU temperature, Celsius | any number | Negative or above 150 means unavailable |
| notification_sequence | Event counter | non-negative integer | Must change to signal an event |

Numbers may be integers or decimals. Anything else rejects the whole line: letters, `inf`, `nan`, hex, empty fields.

## Strict parsing

- Exactly 7 values after the `A`. Lines with fewer values (the old five-value format) or more are discarded completely.
- A rejected line never updates any state.
- Lines longer than 95 bytes are discarded.
- A half-received line is dropped after 200 ms without new bytes, so a restart of the PC program cannot glue two lines together.

## Notification events

- The first valid packet after boot or after a link loss only records the counter. It never fires an event.
- A different counter value must appear in two consecutive packets before the animation starts. This ignores a single corrupted packet.
- Repeating the same counter value never retriggers the animation.
- If another event arrives during the animation, the animation restarts from LED 1.

## Timing the PC program must respect

- Send continuously. 20 to 60 packets per second is typical.
- Audio is treated as silent after 0.7 s without packets.
- The PC link is considered lost after 3 s without a valid packet. Alerts clear immediately and the amber spinner shows.
- Opening the COM port resets most NodeMCU boards through the DTR line. Expect the boot sweep every time the PC program starts, and wait about 2 to 3 seconds before sending.

## Minimal sender

```python
import serial, time
ser = serial.Serial("COM5", 115200)
time.sleep(3)                      # board resets when the port opens
seq = 0
while True:
    line = f"A,{audio},{bass},{mid},{treble},{cpu},{temp:.1f},{seq}\n"
    ser.write(line.encode("ascii"))
    time.sleep(1 / 30)
```

Use `temp = -1` when the sensor is unavailable. Increase `seq` by one for each notification.

## No checksum

The format has no checksum, so a corrupted line that still looks valid cannot be detected. Corrupted notification counters are covered by the two-packet rule above. Over USB serial corruption is rare.
