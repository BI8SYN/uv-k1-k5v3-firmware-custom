#!/usr/bin/env python3
# APRS RX read-only assets: the screen texts and the correlator tables.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The Mic-E message names are a fixed-stride table read straight into the row.
# The tables are 127 * cos(2 pi f n / 9600): one period of 1200 Hz (8 samples)
# and of 2200 Hz (48 samples = 11 cycles). The sine is the same table read 3/4
# of a period ahead (+6 and +12), as in test/model_rx.py.
#
#   ./gen_assets.py aprsrx_assets.bin aprsrx_assets.h
import math, os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

FS = 9600
TITLE = "APRS RX"                           # the version is in the .app header
WAIT_CAPS = "WAIT"                          # status-bar capsule until a frame

UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT_CAPS),
    ("T_DBM",    "dBm  sl "),
    ("T_OK",     "ok "),
    ("T_KMH",    "km/h "),
    ("T_CUSTOM", "Custom-"),
]

# Mic-E message types, indexed by the destination's message bits A B C
MIC_MSG = ["Emergency", "Priority", "Special", "Committed",
           "Returning", "In Service", "En Route", "Off Duty"]

def cos_table(f):
    per = FS // math.gcd(FS, f)
    return [int(round(127 * math.cos(2 * math.pi * f * n / FS))) for n in range(per)]

a = Assets("APRSRX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the UI block read by draw()
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT_CAPS))
a.table("T_MSG", MIC_MSG)                    # Mic-E standard messages
a.i8("COS1200", cos_table(1200))            # 8 entries
a.i8("COS2200", cos_table(2200))            # 48 entries
a.main()
