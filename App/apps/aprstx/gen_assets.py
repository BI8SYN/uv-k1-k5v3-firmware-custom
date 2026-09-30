#!/usr/bin/env python3
# APRS TX read-only assets: the screen texts and the fixed part of the frame.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The frame parts are AX.25-encoded here (addresses shifted left, SSID bytes,
# end-of-address bit on the last path entry): the app adds the source
# (boot-message callsign + SSID), the position (edited on the radio, this one is
# the default) and the FCS. Edit the station settings below, then rebuild;
# test/tx_model.py checks the resulting frame.
#
#   ./gen_assets.py aprstx_assets.bin aprstx_assets.h
import os, re, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

# ---- station settings ----
SSID = 7                                   # F4HWN-7
DEST = "APZK5"                             # APZ = experimental software
PATH = ["WIDE1-1"]                         # [] for none
LAT, LON = "4850.90N", "00216.25E"         # default position: DDMM.hhN, DDDMM.hhE
SYMBOL = "/["                              # table, code: '[' = person
COMMENT = "UV-K5 APRS TX"
# info field: "!" + LAT + SYMBOL[0] + LON + SYMBOL[1] + COMMENT

TITLE = "APRS TX"
TX_CAPS = "TRANSMIT"                       # status-bar capsule while on the air

UI = [
    ("T_TITLE",  TITLE),
    ("T_TX",     TX_CAPS),
    ("T_DENIED", "TX denied"),
    ("T_NOCALL", "No boot callsign"),
    ("T_LVL",    "lvl "),
    ("T_TW",     "  tw "),
    ("T_SENT",   "  sent "),
    ("T_LAT",    "LAT  "),              # 2 spaces: digits aligned with LON
    ("T_LON",    "LON "),
    ("T_HELP1",  "0-9 digit  * N/S E/W"),
    ("T_HELP2",  "UP/DN move  MENU ok  EXIT"),
    ("T_BADPOS", "Invalid position"),
]


def addr(call, last=False):
    call, _, ssid = call.upper().partition("-")
    if not (1 <= len(call) <= 6 and call.isalnum()):
        sys.exit(f"bad AX.25 address {call!r}")
    b = bytes(ord(c) << 1 for c in call.ljust(6))
    return b + bytes([0x60 | (int(ssid or 0) << 1) | (1 if last else 0)])


if not 0 <= SSID <= 15:
    sys.exit("SSID must be 0-15")
if len(PATH) > 2:
    sys.exit("at most 2 path entries (frame buffer)")
if not (re.fullmatch(r"\d{4}\.\d{2}[NS]", LAT) and re.fullmatch(r"\d{5}\.\d{2}[EW]", LON)):
    sys.exit("LAT must be DDMM.hhN/S, LON DDDMM.hhE/W")
if len(SYMBOL) != 2 or len(COMMENT) > 43:
    sys.exit("SYMBOL is 2 characters, COMMENT at most 43 (frame buffer)")
# 13 digits (DDMMhh DDDMMhh), then the hemispheres: bit 0 south, bit 1 west
POS = [int(c) for c in LAT + LON if c.isdigit()]
HEMI = (LAT[-1] == "S") | (LON[-1] == "W") << 1

a = Assets("APRSTX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the UI block read by draw()
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_TX_CHARS", len(TX_CAPS))
a.const("CFG_SSID", SSID)
a.raw("F_DEST", addr(DEST))
a.raw("F_PATH", b"".join(addr(h, i == len(PATH) - 1) for i, h in enumerate(PATH)))
a.u8("POS_DEF", POS + [HEMI])
a.raw("F_SYM", SYMBOL.encode("ascii"))
a.raw("F_COMMENT", COMMENT.encode("ascii"))
a.main()
