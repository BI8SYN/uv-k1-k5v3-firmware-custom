# AIS RX 1.1

Receive-only AIS overlay for F4HWN Labs ABI 1 / API 2. It uses the currently
selected RX VFO, following the other receiver apps. It does not retune the
radio, select a different RF path, scan or transmit.

## Use

Select the desired channel or frequency in FM before opening the Apps menu.
The usual AIS channels are 161.975 MHz (AIS 1) and 162.025 MHz (AIS 2); save them
as normal radio channels if convenient. The app itself contains no frequency
presets. Exit to change the selected VFO or channel.

| Key | Action |
| --- | --- |
| 0 | Toggle speaker monitoring; off on launch |
| MENU | Switch between the latest report and RX statistics |
| * | Clear the latest report and counters |
| EXIT / PTT | Exit; PTT does not transmit inside the app |

The status bar shows WAIT until the first accepted report, then the accepted
report count, plus the speaker icon and battery. The body shows the latest
MMSI and signed decimal-degree coordinates. The compact telemetry row shows
`S` = speed over ground in knots, `C` = course over ground in degrees (both to
0.1), `H` = true heading, and `T` = message type. `---` means unavailable data.
The selected RX frequency appears below the dotted separator in large digits,
with its final two digits in the small font.

Types 1/2/3/18 include position and motion data. Other valid types show their
MMSI/type; position fields remain unavailable. Only CRC-valid reports are
accepted. The count saturates at 65535. Frames larger than 64 bytes including
FCS are discarded. Reports stay in RAM and are discarded on exit. No settings
or received data are written to flash. There is no map, history, simultaneous
dual-channel reception or multi-slot reassembly.

The speaker icon means monitoring is enabled; actual audio follows the selected
channel's hardware squelch result. CTCSS/DCS gating is not implemented. Decoder
operation is independent of the speaker and squelch gate.

## Statistics

`OK` is the accepted-report count, `L` counts missed sample deadlines,
`ADC timeout` counts failed conversions, and `P-P` is the last five-second ADC
peak-to-peak span (0–4095). Planned screen/key/battery maintenance gaps are
excluded from L. Maintenance is deferred during a candidate frame but remains
bounded for key responsiveness. A frame starting during a gap may be lost.

## Build and checks

```sh
./compile-app.sh aisrx
App/apps/aisrx/tests/run.sh
```

Output: `build/Apps/AISRX.app`. Code/data/BSS: **3528 B** of the 4096 B budget.
No assets, recordings, replay mode or test-data controls are included.

Developer tests use synthetic HDLC/NRZI waveforms rather than captured data.
ASan/UBSan checks cover parser sentinels and signed positions, CRC corruption,
stuffing/abort/overflow/resync, ADC timeout/deadline recovery, selected-VFO
preservation, absence of asset access, UI bounds and register restoration.

The receiver samples PA4 at 48 kHz and runs DC tracking, a sign slicer/DPLL,
NRZI, HDLC and CRC-16/X.25. It temporarily disables voice filters/de-emphasis
and AFC, selects a wide IF filter, and restores changed ADC/DAC/GPIO/BK state
on exit. Analog filtering remains a hardware constraint: successful software
tests do not establish antenna-to-ADC reception performance. **Live RF reception
is awaiting external validation.** This app is not a navigation or safety device.
