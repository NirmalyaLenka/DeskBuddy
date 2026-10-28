#!/usr/bin/env python3
"""
Desk Buddy serial test sender.

Sends the same line format as the real PC program:
    A,audio,bass,mid,treble,cpu,temp,notification_sequence

Use it to test the firmware without the real monitor running.
Close the real Python program first: only one program can hold the COM port.

    pip install pyserial
    python serial_test_sender.py COM5 demo
    python serial_test_sender.py COM5 music
"""

import argparse
import math
import random
import sys
import time

import serial

BAUD = 115200
RATE_HZ = 30


def music_levels(t):
    """Synthetic 120 BPM track: kick on the beat, wandering mids, hi-hats on off-beats."""
    beat = (t * 2.0) % 1.0
    kick = math.exp(-beat * 7.0)
    bass = int(min(255, 40 + 215 * kick))
    mid = int(min(255, 70 + 60 * math.sin(t * 1.7) ** 2 + 40 * random.random()))
    hat = 90 if (beat % 0.5) < 0.06 else 0
    treble = int(min(255, 30 + 110 * random.random() ** 2 + hat))
    audio = int(min(255, 0.6 * bass + 0.3 * mid + 0.1 * treble + 20))
    return audio, bass, mid, treble


class Sender:
    def __init__(self, port):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        print("Opened %s. Waiting 3 s for the NodeMCU to reset and finish its boot sweep..." % port)
        time.sleep(3.0)
        self.seq = 0

    def send(self, audio=0, bass=0, mid=0, treble=0, cpu=10, temp=45.0):
        line = "A,%d,%d,%d,%d,%d,%.1f,%d\n" % (audio, bass, mid, treble, cpu, temp, self.seq)
        self.ser.write(line.encode("ascii"))

    def stage(self, label, seconds, music=False, cpu=10, temp=45.0, notify_at=()):
        print("-> %s (%d s)" % (label, seconds))
        start = time.time()
        pending = sorted(notify_at)
        while True:
            t = time.time() - start
            if t >= seconds:
                break
            if pending and t >= pending[0]:
                pending.pop(0)
                self.seq += 1
                print("   notification, sequence=%d" % self.seq)
            a, b, m, tr = music_levels(t) if music else (0, 0, 0, 0)
            self.send(a, b, m, tr, cpu, temp)
            time.sleep(1.0 / RATE_HZ)

    def silence(self, label, seconds):
        print("-> %s (%d s, no data sent)" % (label, seconds))
        time.sleep(seconds)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="COM port, for example COM5")
    ap.add_argument("scenario", choices=[
        "idle", "music", "temp", "cpu", "both", "notify", "notemp", "disconnect", "demo"])
    args = ap.parse_args()

    s = Sender(args.port)
    try:
        sc = args.scenario
        if sc == "idle":
            s.stage("idle: connected, silent", 3600)
        elif sc == "music":
            s.stage("music", 3600, music=True)
        elif sc == "temp":
            s.stage("temperature alert (85 C)", 3600, temp=85.0)
        elif sc == "cpu":
            s.stage("cpu alert (92 %)", 3600, cpu=92)
        elif sc == "both":
            s.stage("combined alert", 3600, cpu=92, temp=85.0)
        elif sc == "notify":
            s.stage("idle, notification every 4 s", 3600, notify_at=[4 * i for i in range(1, 900)])
        elif sc == "notemp":
            s.stage("temperature unavailable (-1), cpu normal", 3600, temp=-1.0)
        elif sc == "disconnect":
            s.stage("idle, link up", 5)
            s.silence("link down", 6)
            s.stage("idle, link restored", 5)
        else:
            s.stage("idle", 6)
            s.stage("music", 40, music=True)
            s.stage("idle (music stopped)", 6)
            s.stage("cpu alert", 6, cpu=92)
            s.stage("temperature alert", 6, temp=85.0)
            s.stage("combined alert", 6, cpu=92, temp=85.0)
            s.stage("temperature unavailable, no alert expected", 5, temp=-1.0)
            s.stage("notification", 4, notify_at=[1.0])
            s.stage("notification during music", 8, music=True, notify_at=[3.0])
            s.stage("notification during alert", 6, cpu=92, notify_at=[2.0])
            s.stage("idle", 3)
            s.silence("link down", 6)
            s.stage("idle, link restored", 5)
            print("Demo finished.")
    except KeyboardInterrupt:
        pass
    finally:
        s.ser.close()


if __name__ == "__main__":
    sys.exit(main())
