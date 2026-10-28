# Behaviour

## Priority order

Highest first. The active mode is recomputed every frame from current state, so nothing can stay stuck after an alert or notification ends.

1. Boot sweep (first 1.5 s only)
2. Notification animation
3. CPU alerts: combined, then temperature, then utilization
4. Audio-reactive scenes
5. Idle spinner (PC connected, no music)
6. Link-lost spinner (no valid packet for 3 s)

Mode changes cross-fade, except entering a notification, which cuts in instantly.

## Idle

A blue-teal comet with a fading tail circles clockwise over a faint base ring so the six LEDs read as a circle. Speed eases slightly around the ring and the hue drifts slowly.

## Link lost

The same spinner in dim amber and slower. Set `SHOW_NOLINK_INDICATOR = false` to show the idle spinner instead.

## CPU alerts

- Temperature above 80 C: all six LEDs breathe red.
- Utilization above 80 percent: all six LEDs breathe pink.
- Both: LEDs 1, 2, 3 red and LEDs 4, 5, 6 pink, following the clockwise numbering.
- The condition must hold 0.4 s before the alert shows, the alert stays at least 1.5 s, and it clears 2 C or 3 percent below the threshold. This prevents flicker.
- An unavailable temperature (negative or implausible value) never raises or keeps an alert.

## Notification

White at full brightness. The lit LED steps from LED 1 to LED 6, three times, with a short fade behind it, about one second in total. The mode that applies afterwards resumes with a short fade.

## Audio

Music mode starts when the raw level stays above a threshold for 120 ms and ends after 2 s of near silence. Levels are smoothed with a fast attack and moderate release, and a mild automatic gain (up to 3x) compensates for quiet sources. Beats are detected from bass, falling back to overall level.

Scenes rotate every 18 to 32 s, changing on a beat, and a new scene is chosen each time music starts.

| Scene | Description |
|---|---|
| Orbit | Two comets circle the ring. Loudness sets speed, every beat kicks the rotation, tone sets color. |
| Shockwave | Each beat launches a ripple from a different LED that spreads both ways and meets on the opposite LED. |
| Spectrum | Bass, mid and treble each own a pair of opposite LEDs and bloom wider as they get louder. The pairs rotate by one LED every four beats. |
| Meter | A circular level gauge filling clockwise from LED 1, with a falling peak marker. |
| Sparkle | Treble and mid ignite random twinkles over a slowly rotating bass glow. |

Audio palettes avoid pure red and pink so they never resemble the CPU alerts.
