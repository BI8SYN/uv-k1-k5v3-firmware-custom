# APRS TX: on-radio APRS position beacon (work in progress)

Sends one APRS position frame (AX.25 UI frame, Bell 202 AFSK 1200 bauds) on the
TX VFO at each press of PTT or MENU. Companion of [APRS RX](../aprsrx/README.md),
which is also the test receiver.

| Step | State |
|---|---|
| Frame and bit stream model (`test/tx_model.py`) | **Done**: identical to the RX app's AX.25 reference, decoded by the RX model |
| Radio app (`aprstx_app.c`) | **v0.1 works on the radio** (2026-09-30); v0.2 edits the position on the radio, frequency bottom right as Beacon (builds: 2812 B; radio test pending) |
| On-air test: APRS RX on a second radio, FT3D | **Done** (2026-09-30, v0.1: frames received by a UV-K1 running APRS RX and by the FT3D, first try, default level 66 and twist 0) |

## Station settings (`gen_assets.py`, then rebuild)

| Setting | Default |
|---|---|
| Source | the boot-message callsign (API `boot_callsign`, 1-6 letters/digits) + `SSID` = 7 |
| `DEST` | `APZK5` (APZ = experimental software) |
| `PATH` | `WIDE1-1` (0 to 2 entries) |
| `LAT`, `LON` | `4850.90N`, `00216.25E`: the **default** position, until one is edited on the radio (key 5) |
| `SYMBOL` | `/[` (person) |
| `COMMENT` | `UV-K5 APRS TX` (43 characters at most) |

Frame sent: `F4HWN-7>APZK5,WIDE1-1:!4850.90N/00216.25E[UV-K5 APRS TX` (58 bytes,
~0.73 s on the air: 50 ms of tone settle, 40 flags = 267 ms, the frame, 3 flags).
If the boot message is not a plain callsign (empty, more than 6 characters once
spaces are dropped, or with a `/`), the app shows `No boot callsign` and does not
transmit.

## Keys (UV-K5 and UV-K1)

| Key | Action |
|---|---|
| PTT or MENU | Send one frame |
| UP / DOWN | Tone level (deviation): REG_70 gain 10-127, step 4, default 66 (the firmware's tone gain) |
| 1 / 3 | Twist `tw` -4..+8: 2200 Hz gain = level × (8 + tw) / 8 (-6..+6 dB) |
| 5 | Edit the position |
| EXIT | Quit (level, twist and position are saved) |

### Position editor (v0.2)

```

LAT  48 50.90 N
       ^
LON 002 16.25 E
0-9 digit  * N/S E/W
UP/DN move  MENU ok  EXIT
```

Type the 13 digits in a row, as a frequency: `485090` then `0021625` (degrees,
minutes, hundredths of a minute: the APRS format, read as is from a GPS or
aprs.fi in degrees-minutes). The cursor skips the separators and goes on from
the latitude to the longitude.

| Key | Action |
|---|---|
| 0-9 | Digit at the cursor, then the next one |
| * | N/S (on the latitude) or E/W (on the longitude) |
| UP / DOWN | Move the cursor (to fix one digit) |
| MENU | Check (degrees ≤ 90 / 180, minutes < 60) and keep: the frame is rebuilt; `Invalid position` otherwise |
| EXIT | Cancel |

The position is saved on exit with the level and twist (`cfg_save`, 10 bytes:
magic 0xA8, level, twist, 13 digits and the hemispheres in nibbles). A v0.1
config (magic 0xA7) is ignored: defaults.

## How it transmits

As the Beacon app keys MCW: `tx_set_params` (carrier + PA on the TX VFO),
REG_51 = 0 (no CTCSS/DCS under the AFSK), `tx_tone(1200)` (tone path on, mic ADC
off, 50 ms settle). Then each bit is timed from SysTick, 40000 cycles at 48 MHz
(exact), and at each NRZI transition the tone frequency REG_71 (1200 Hz = 12389,
2200 Hz = 22714, the driver's `scale_freq`) and gain REG_70 are rewritten: two
SPI writes, a few tens of µs out of 833. `tx_mute` + `tx_end` restore RX.

## Unknowns for the first on-air test (all fine with the defaults on 2026-09-30)

1. **Phase continuity** when REG_71 is rewritten (an NCO keeps it, probably).
   The RX model decodes even with a phase reset at every transition (3/3 clean,
   2/3 with noise in STD); a hardware TNC such as the FT3D may be stricter.
2. **Twist.** The tone probably enters after the pre-emphasis, so the signal is
   flat and a receiver with de-emphasis sees 2200 Hz ~5 dB low. If the FT3D
   misses frames that APRS RX decodes, raise `tw` (key 3): +4 is +3.5 dB.
3. **Deviation** at level 66: aim for ~3 kHz; UP/DOWN.

## Test plan

1. Simplex test frequency (not 144.800 for the first frames), low power.
2. Receivers: APRS RX on a second radio (its per-slicer counters `s a/b/c`
   show the twist: frames only on the space-favouring slicer = 2200 Hz low) and
   the FT3D (APRS on band B, same frequency).
3. PTT, check both; adjust level and twist; then 144.800.

`test/tx_model.py` (needs `../aprsrx/test`): frame from the real assets vs
`ax25.build`, bit stream vs `ax25.hdlc_bits`, bad boot callsigns refused, an
edited position (south/west) vs `ax25.build`, the config round trip, the editor
rows and caret, the position limits, then
AFSK (continuous phase or reset, tw 0/+4, TX path twist 0/+5 dB) decoded by the
RX model through its RAW and STD audio paths, with and without noise.
