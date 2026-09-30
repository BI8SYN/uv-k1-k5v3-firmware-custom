# APRS RX: on-radio APRS receiver (work in progress)

Goal: an overlay app that receives APRS (AX.25 UI frames, Bell 202 AFSK 1200
bauds) on the UV-K1 / UV-K5 v3 and shows the last frame, then an APRS TX app.

Status:

| Step | State |
|---|---|
| Hardware demodulation by the BK4829 FSK block | **Dead end** (2026-09-25, FT3D on 144.800: the chip has no Bell 202 demodulator) |
| Audio path an app can sample | **Done** by EPIRB 406: RX audio on PA4, ADC channel 4 at 9.6 kHz |
| Integer demodulator, modelled on synthetic audio (`test/model_rx.py`) | **Done** (see below) |
| Flipper Zero test transmitter (`test/flipper_aprs.py`) | **Done**, files in `test/flipper/` |
| Radio app (`aprsrx_app.c`) | **v0.1 receives the Flipper frames on the radio**; v0.3 decodes Mic-E (on air: F5RAV via F1PRY-14); v0.4 decodes continuously with 3 slicers (on air: F1PRY-14 via F5KTR-3); v0.5 shows standard positions, the frequency bottom right as Beacon, drops `pp` and the UP/DOWN/5 tuning offset to fit 4 KiB (builds: 3976 B, 120 B free; radio test pending) |
| Bench test with the Flipper on 433.650 MHz | **Done** (2026-09-30, v0.1: `_long` 10/10 in STD; `_badfcs` not shown) |
| Real station: FT3D beacon on 144.800 MHz | **Done** (2026-09-30, v0.1: Mic-E frame `>TXUPX9` received, FCS good, = 48°50.89' N 2°16.25' E) |
| Mic-E decoding (`test/mice.py`: spec encoder vs the app's decoder) | **Done** in the model: 6 cases + the FT3D frame |

## Using the app

1. Set the VFO to the APRS frequency, **FM**: 144.800 MHz, or 433.650 MHz for the
   Flipper test files.
2. Launch **APRS RX**.
3. Lower the volume: the RX audio must stay on (it is what reaches PA4), so the
   speaker plays the channel.

Since v0.4 the app decodes **continuously**, as a TNC does: no RSSI trigger,
the FCS and a UI-frame check (control 0x03, PID 0xF0) sort frames from noise.
Up to v0.3, captures started 10 dB above a tracked noise floor; on a busy
144.800 that floor crept up (−132 → −104 dBm), so weak stations were never
sampled. Several frames in one transmission are now received too.

Keys and the screen cost sample time (the key scan alone is ~400 µs), so they
are served every 50 ms only while no slicer is inside a preamble (two
back-to-back flags) or a frame whose first bytes look like a callsign (busy
~4 % of the time on noise, in the model), and after 3 s of busy at the latest.
The screen is redrawn after a new frame, a key, or every 5 s.

| Screen | Content |
|---|---|
| Status bar | `APRS RX` title, then a `WAIT` capsule until the first frame |
| Line 0 | Source call and frame number, e.g. `F4HWN-7 #3` |
| Tiny row y=8 | `>DEST,DIGI*,...` (as many path entries as fit on one row) |
| Tiny rows y=13/19/25 | Info field, 3 × 32 characters (non-ASCII shown as `.`); for Mic-E (most Yaesu/Kenwood beacons, v0.3): position `48 50.89N 002 16.25E`, then speed km/h, course, symbol and message type (`Off Duty`, `En Route`...), then altitude and comment without the device markers; for an uncompressed position (`!` `=` `/` `@`, v0.5): position, then the timestamp if any, the symbol (table + code) and the comment over two rows. Compressed positions stay raw text |
| Lines 4-5 | Under a dotted separator (y=30), the frequency drawn as the main screen draws it (big digits up to the kHz, the last two in the small font), centred; same code as APRS TX |
| Bottom row y=49 | `ok 12  -89dBm  sl 5/12/7`: frames received, RSSI at the end of the last frame, frames per slicer (space-favouring / neutral / mark-favouring; a frame found by several slicers counts in each, but once in `ok`) |

Keys (UV-K5 and UV-K1):

| Key | Action |
|---|---|
| MENU | Clear the last frame and the counters |
| EXIT | Quit |

The receive path is the firmware's own (STD: 300 Hz high-pass, de-emphasis,
3 kHz low-pass). Up to v0.3, key 1 switched to RAW (filters and AFC off, as
EPIRB 406); every on-air decode was made in STD and the model shows no gain
for RAW, so v0.4 dropped it to fit the 4 KiB overlay.

The last good frame is kept on `app_main`'s stack and the demodulator state on
`capture`'s: the 4 KiB overlay also holds `.bss`. The screen texts and the two
cos tables are assets.

## Demodulator (`test/model_rx.py`, class `Demod`; the C is a transcription)

Per ADC sample (9.6 kHz, 8 samples per bit):

1. DC removal (1-pole tracker, 64 samples).
2. Band-pass biquad centred on √(1200·2200) = 1625 Hz, Q 0.9 (Q14): equal gain
   on both tones, removes the out-of-band noise the box correlators let through.
3. Four sliding correlators over one bit: I/Q at 1200 Hz (8-entry cos table) and
   2200 Hz (48 entries = 11 cycles). The sine is the cos table read 3/4 period
   ahead (+6, +12). Products `>> 8`, so the decision below never overflows int32.
4. Magnitudes `max + 3/8 min`, per-tone peak trackers (attack 1/32, decay
   1/1024): each tone normalised by its own peak, so the twist (pre-emphasis or
   not, de-emphasis or not) does not bias the decision much.
5. **3 slicers** (since v0.4), decision `sign(wa·Mm·Ps − wb·Ms·Pm)` with
   wa:wb = 2:3, 1:1, 3:2, each with its own DPLL, NRZI and HDLC below (Direwolf's
   multi-slicer idea): the AGC does not cancel a strong twist in noise. A frame
   from any slicer is accepted; a copy from another slicer within 1 s is dropped.
6. DPLL: 65536 per bit, a bit at each wrap; each transition pulls the phase 1/4 of
   the way toward mid-bit + half a sample (the transition is only seen at the
   next sample: without that half-sample, a +1 % clock failed long frames).
7. NRZI, HDLC (flag, destuffing, abort after 7 ones), CRC-16/X.25 per byte,
   frame accepted on the residue 0xF0B8 **and** as a UI frame: noise brings ~0.7
   candidates per second to the FCS check, so without the UI check a false frame
   would pass about once a day.

### Slicers (`test/variants.py`, 4 seeds, 24 hard cases: noise 3000-4000 Hz, ±1 % clock, twist −9 to +9 dB at noise 2500 Hz, RAW and STD)

| Variant | Frames /96 |
|---|---|
| 1 slicer (v0.1-v0.3) | 57 |
| 3 slicers, weights ×2 | 65 |
| **3 slicers, weights ×1.5 (v0.4)** | **71** |
| 5 slicers (×1.5 and ×2) | 71 |

The gain is on strong twist: sine AFSK at +6 / +9 dB in RAW goes from 0/4 to 4/4.
Noise only (30 s, 2 seeds, 50 and 400 LSB rms): no frame accepted.

### Model results (`test/model_rx.py`, 3 seeds per case, 1 slicer; 3 slicers: all ≥ these)

Channel: discriminator output + white noise (rms over 48 kHz) → radio audio path
(RAW: 5 kHz low-pass; STD: 300 Hz high-pass, 750 µs de-emphasis, 3 kHz low-pass)
→ 12-bit ADC at 9.6 kHz, 0.065 LSB/Hz (EPIRB 406 measurement), optional clock
error. 150 ms of no-carrier noise before and after each burst.

| Case | RAW | STD |
|---|---|---|
| Flipper, clean: `pos`, `long` (233 bytes) | 3/3, 3/3 | 3/3, 3/3 |
| Flipper `badfcs` (must be rejected) | 0 frames | 0 frames |
| Flipper, noise 1500 / 3000 / 4500 Hz | 3/3, 2/3, 0/3 | 3/3, 2/3, 0/3 |
| Flipper, ±4 kHz carrier offset | 3/3 | 3/3 |
| Flipper `long`, ±1 % sample clock | 3/3 | 3/3 (−1 %), 3/3 (+1 %) |
| Sine AFSK (a real station), twist −6 / 0 / +6 dB, noise 1500 | 3/3, 3/3, 2/3 | 2/3, 3/3, 3/3 |

Noise 4500 Hz is Eb/N0 ≈ 9.6 dB: the theoretical frame success of non-coherent
FSK on a 520-bit frame is then ~6 %, so the failures there are expected.
(At ±0.5 % clock, 6/6 in every variant tried.)

## Flipper Zero test transmitter (`test/flipper_aprs.py`)

The Flipper's CC1101 cannot put an audio tone on FM, only switch between two
frequencies (preset `2FSKDev238Async`, ±2.38 kHz). Each AFSK tone is therefore
sent as a square wave: the carrier toggles at twice the tone frequency,
phase-continuous across bits, with edge times rounded from exact values (no
drift). The receiver's discriminator outputs that square wave and its audio
low-pass leaves mostly the fundamental. The Flipper cannot reach 2 m anyway; the
script refuses 144-146 MHz.

```
test/flipper_aprs.py test/flipper [--call F4HWN] [--freq 433650000]
```

writes `aprs_pos.sub`, `_digi`, `_msg`, `_status`, `_long` (233 bytes, 2 s) and
`_badfcs` (one info bit flipped: must not be shown). 100 ms of carrier, 40 flags
(267 ms), the frame, 3 flags, 10 ms of carrier. Copy them to `subghz/` on the
Flipper, set the radio to 433.650 MHz FM, launch the app, then Send one file at
a time, about 1 s apart. (Up to v0.4, UP/DOWN tuned ±5 kHz for a Flipper whose
crystal puts the carrier off the channel; never needed on the bench, dropped in
v0.5 to fit 4 KiB.)

`test/ax25.py` builds the frames (shared by the generator and the model);
`test/mice.py` checks the Mic-E display decoder against a spec encoder, and
the standard position display against real frames;
`test/variants.py` compares demodulator variants on the hard cases (dev tool).

## Bench checklist

- Level reaching the ADC (`pp`, shown up to v0.4, dropped in v0.5 to make
  room): estimated ~300 LSB peak-to-peak for the Flipper (±2.38 kHz at
  0.065 LSB/Hz), **measured 734-778** with the Flipper and **916** with a real
  station (F1PRY-14): ~20 % of full scale, plenty of headroom, and the per-tone
  AGC makes the absolute level irrelevant.
- The per-slicer counters show which decision threshold the stations need: if
  one outer slicer does most of the work, the twist of the path is off-centre.
