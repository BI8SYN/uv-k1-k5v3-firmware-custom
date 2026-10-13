# AIS RX 1.0

Experimental receive-only AIS overlay for F4HWN Labs ABI 1 / API 2.
The recorded-audio decoder and on-device replay have been tested. **Live RF
reception has not been validated with a known AIS source.**

## Operation

Launch AIS RX from the Apps menu. It temporarily tunes to AIS 1 (161.975 MHz),
using one channel at a time. EXIT or PTT exits without transmitting and the
loader restores the original VFO. No app settings or received messages are
written to flash.

| Key | Action |
| --- | --- |
| 1 | AIS 1, 161.975 MHz; return from replay to live reception |
| 2 | AIS 2, 162.025 MHz; return from replay to live reception |
| 0 | Toggle speaker monitoring; off on launch |
| 5 | Replay two short real recorded-audio fixtures through the sample decoder |
| MENU | Toggle sampling diagnostics / frequency row |
| * | Clear the displayed report and counters |
| EXIT / PTT | Exit; PTT does not transmit |

LIVE identifies experimental ADC reception. REPLAY identifies recorded data;
no live samples are decoded while that result is displayed. Replay resets the
report count, decodes both fixtures, and shows the second (Helsinki) result:
MMSI **230991740**, latitude **+060.16971**, longitude **+024.97456**, type 3,
OK **00002**. The first fixture is MMSI **366998680** (Long Beach).

Only CRC-valid reports are accepted. The app shows the latest MMSI, message
type and accepted-report count (saturating at 65535). Types 1/2/3/18 additionally
show signed decimal-degree coordinates, speed over ground in knots, course over
ground in degrees (both to 0.1), and true heading. `---` marks unavailable data.
Other types show their header; positions from unsupported types are not inferred.
Frames larger than 64 bytes including FCS are discarded. There is no map,
message history, simultaneous dual-channel reception or multi-slot reassembly.

The speaker icon means monitoring is enabled; actual audio follows the inherited
hardware squelch result. CTCSS/DCS gating is not implemented. Replay is muted.

Diagnostics: `L` counts missed sample deadlines, `T` counts ADC timeouts,
`P` is the last five-second ADC peak-to-peak span, capped at 999. Intentional
housekeeping gaps are excluded from L. A good L/T result proves scheduling,
not RF bandwidth or decoding sensitivity. Normal callbacks may miss a frame
that begins during housekeeping; sampling resumes with a new frame candidate.

## Implementation

48 kHz PA4 ADC (five samples per bit), symmetric DC tracking, sign slicer/DPLL,
NRZI decoding, HDLC bit destuffing and CRC-16/X.25. RX voice filters/de-emphasis
and AFC are disabled, and the VHF path and wide equal strong/weak IF filters are
selected temporarily. ADC/DAC/GPIO and changed BK registers are restored.
This raw analog path still needs RF validation at 9600 bit/s.

Code/data/BSS: **3488 B** of 4096 B. Replay assets: **3584 B** of 3840 B.
The 3584 replay samples are stored as signed 8-bit 48 kHz demodulated audio,
not IQ, read in small blocks outside live sampling.

## Build and test

```sh
./compile-app.sh aisrx
App/apps/aisrx/tests/run.sh
```

Output: `build/Apps/AISRX.app`. Tests need a C compiler with ASan/UBSan and
Python 3. They cover parser bounds/sentinels, negative positions, type 18,
CRC corruption, HDLC stuffing/abort/overflow/resync, exact recorded payloads
with inverted polarity, noise rejection, timed sample input, deadline/ADC
failure recovery, UI bounds, channel/audio controls and register restoration.

## Recorded-audio provenance and limits

Fixtures are short windows from publicly downloadable recordings documented at
https://github.com/freerange/ais-on-sdr/wiki/Example-data-files . Their sample
offsets, payloads and hashes are in `tests/fixtures.json`. Original source:
Long Beach WAV (8-bit mono, 48 kHz); Helsinki raw (signed 16-bit stereo, 48 kHz,
channel 0). The fixture reports also appear in independent GNU AIS output.
`tests/prepare_recordings.py` regenerates these windows from the original files.

Full recordings were decoded on the host: Long Beach WAV 80 reports, Helsinki
channels 142 + 24 reports, Long Beach lossy OGG 16 reports. GNU AIS comparison
counts were 73, 153 + 52, and 17 respectively. Counts differ and do not imply
all decoders recover identical reports. The compact decoder has weaker noise
margin than a full SDR receiver: added noise can cause a valid frame to be lost.
A known RF source or reception near real AIS traffic is still required to
validate the antenna-to-ADC chain. Do not use this experimental receiver for
navigation or safety decisions.
