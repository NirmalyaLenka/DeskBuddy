# Tuning

All settings are constants in the CONFIGURATION block at the top of `DeskBuddy.ino`.

## Hardware

| Setting | Purpose |
|---|---|
| `LED_PIN` | Data pin, default 14 (D5) |
| `LED_STRIP_TYPE` | Color order and speed |
| `RING_REVERSED`, `LED_INDEX_OFFSET` | Match logical LED 1 and clockwise direction to the ring |

## Brightness

`BR_BOOT`, `BR_IDLE`, `BR_NOLINK`, `BR_AUDIO`, `BR_ALERT`, `BR_NOTIFY`: roughly the peak LED byte value (0 to 255) in each mode. `GAMMA` sets perceptual fading.

## Alerts

`TEMP_ALERT_C`, `TEMP_HYST_C`, `CPU_ALERT_PCT`, `CPU_HYST_PCT`, `ALERT_ENTER_MS`, `ALERT_MIN_HOLD_MS`, `ALERT_BREATH_PERIOD_S`, `ALERT_BREATH_DEPTH` (0 for a steady glow). `COLOR_RED` and `COLOR_PINK` are final LED byte values; adjust `COLOR_PINK` if it looks too white or too purple on your LEDs.

## Notification

`NOTIF_PASSES`, `NOTIF_STEP_MS` (smaller is faster), `NOTIF_TAIL_STEPS`, `NOTIF_CONFIRM_PACKETS` (1 fires on the first changed packet), `COLOR_NOTIFY`.

## Link

`AUDIO_STALE_MS`, `LINK_TIMEOUT_MS`, `SHOW_NOLINK_INDICATOR`.

## Audio sensitivity

| Setting | Purpose |
|---|---|
| `AUDIO_ON_THRESHOLD`, `AUDIO_OFF_THRESHOLD` | Raw 0 to 255 levels that start and end music mode |
| `AUDIO_HOLD_MS` | Silence time before returning to idle |
| `AUTO_GAIN`, `AGC_*` | Automatic gain for quiet sources |
| `GAIN_BASS`, `GAIN_MID`, `GAIN_TREBLE` | Per-band weighting |
| `ENV_ATTACK_S`, `ENV_RELEASE_S` | Smoothing: smaller is snappier |
| `BEAT_MIN_LEVEL`, `BEAT_MARGIN`, `BEAT_REFRACTORY_MS` | Beat detection |

## Scenes

`AUDIO_SCENE_LOCK` set to 0 to 4 keeps one scene (orbit, shockwave, spectrum, meter, sparkle). `-1` rotates automatically. `SCENE_MIN_MS` and `SCENE_MAX_MS` set rotation timing. `ORBIT_*`, `WAVE_*`, `BEATS_PER_SPECTRUM_STEP` and `METER_*` shape individual scenes.

## Debug

`DEBUG_STATUS = true` prints a status line per second to the USB serial port. Leave it off in normal use: if no program is reading the port, blocking transmit can stall the animation.
