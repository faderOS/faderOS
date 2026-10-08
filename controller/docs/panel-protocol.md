# BKDS binary link, protocol version 1

The current firmware reports application version separately from protocol version.
Serial starts at 9600 baud, 8N1, full duplex on SCI2. 38400 has physical validation;
76800 remains experimental. All multi-byte wire fields are big-endian. C struct
padding is never transmitted. This is a trusted internal link, without authentication.

## Framing

COBS-encode the following bytes and terminate with `00`:

| Field | Bytes |
| --- | ---: |
| Protocol version, 1 | 1 |
| Type | 1 |
| Sequence | 2 |
| Session | 4 |
| Payload length, 0–192 | 2 |
| Payload | variable |
| CRC-32/ISO-HDLC, compatible with zlib.crc32 | 4 |

CRC covers header and payload. Maximum decoded frame size is 206 bytes, maximum
wire size 208 bytes. Invalid COBS, version, length or CRC discards the frame and
resynchronizes at the next delimiter. A delimiter precedes binary boot traffic.

## Sessions and reliability

The host selects a nonzero random 32-bit session and sends HELLO with sequence 0.
HELLO_REPLY contains capabilities, catalog revision and the baseline snapshot.
EVENT_ACK with sequence 0 enables events; the first valid command can also
confirm the handshake. Repeated HELLO for the same session returns the same
baseline without clearing outputs or the queue. A new session clears outputs,
queued events and sequences. Ordinary frames from another session are ignored.

Command and reliable-event sequences are independent: 1–65535, wrapping to 1.
Only one unacknowledged command and one unacknowledged reliable event are active
per direction. Retrying the identical sequence/CRC returns the previous result
without repeating the operation. Conflicting sequence reuse returns status 3.
Argument errors consume the command sequence and are repeatable.

The panel keeps up to 16 pending control snapshots and retransmits its oldest
event every 500 ms until acknowledged. The host deduplicates before acting.
The firmware watchdog is currently 5000 ms (not the earlier prototype's 2 s).
Loss or queue overflow clears outputs and abandons the session. Presses during
an outage cannot be reconstructed. The host resends desired outputs after sync.

## Message types

Types are hexadecimal. H = host, P = panel.

| Type | Direction | Payload |
| --- | --- | --- |
| 01 HELLO | H → P | Empty; sequence 0, nonzero session |
| 02 PING | H → P | Empty |
| 03 SYNC | H → P | Empty; requests a queued control snapshot |
| 04 SET_BAUD | H → P | Nominal baud u32; only from 9600 |
| 05 EVENT_ACK | H → P | Empty; event sequence, or 0 for baseline |
| 06 BAUD_CONFIRM | H → P | Nominal baud u32 |
| 07 ANALOG_MODE | H → P | u8=1; enable split analogs for this session |
| 08 GET_FIRMWARE_INFO | H → P | Empty |
| 10 LED_BATCH | H → P | 1–96 pairs of id:u8, state:u8 |
| 11 LCD_LINE | H → P | Row 0/1 and exactly 40 ASCII characters, 32–126 |
| 12 SEGMENTS | H → P | Six native bytes plus AUTO/DSK/FTB bits 0/1/2 |
| 13 INDICATORS | H → P | One byte, only bits 4–7 |
| 14 BUZZER | H → P | u8=0/1 |
| 15 LCD_PATCH | H → P | Offset 0–79, then 1–80 ASCII characters; total ≤80 |
| 18 LCD_ICONS | H → P | First slot 0–7, then 1–8 ROM icon IDs (1–8) |
| 16 LCD_GLYPH | H → P | First slot 0–7, then 1–8 contiguous glyphs (8 row masks each) |
| 17 LCD_CELLS | H → P | Offset 0–79, then 1–80 raw character bytes; total ≤80 |
| 30 ENTER_UPDATE | H → P | ASCII UPD1, only at 9600; ACK precedes RAM updater |
| 80 ACK | P → H | Status u8, matching command sequence |
| 81 HELLO_REPLY | P → H | Catalog revision u16=1, capability bits u16, snapshot |
| 82 STATE | P → H | Legacy snapshot, reliable event sequence |
| 83 TBAR | P → H | Sample time u16 ms, raw value u16 (0–4095) |
| 84 JOYSTICK | P → H | Sample time u16 ms, X:u8, Y:u8 |
| 85 CONTROLS | P → H | Time u32 ms, 23 button bytes, 6 encoder u32s |
| 86 DIAGNOSTICS | P → H | Eight u32 counters |
| 87 FIRMWARE_INFO | P → H | Schema u8=1, ASCII version[16], build date[12] |

EVENT_ACK has no response. HELLO_REPLY replaces HELLO's ACK. FIRMWARE_INFO uses
the request sequence/session; its strings are NUL-terminated and zero-padded.
Other commands receive ACK. SYNC additionally queues a STATE/CONTROLS event.

ACK statuses: 0 accepted, 1 invalid type/arguments, 3 sequence conflict,
4 incompatible colors in a shared bus. Invalid batches never partially change
outputs. LCD ACK means desired contents were accepted; physical writes may
finish later. LCD_PATCH can cross row boundaries at offset 40.

Capabilities: bits 0–3 LEDs/LCD/segments+indicators/buzzer; bit 4 LCD_PATCH;
bit 5 baud negotiation; bit 6 RAM updater; bit 7 split analogs; bit 8 firmware
identification; bit 9 LCD_GLYPH + LCD_CELLS; bit 10 LCD_ICONS. Test capability bits instead of assuming an application version.

## Baud negotiation

SET_BAUD accepts 19200, 38400 or 76800 in recent firmware. The panel ACKs at
9600, drains TX, waits 200 ms and changes SCI2. The host changes its UART,
waits 300 ms and sends BAUD_CONFIRM. Sampling continues during negotiation,
but events are suspended. Lack of confirmation within 1500 ms returns the panel
to 9600 and abandons the session. Watchdog loss also restores 9600.

Before HELLO, the host restores 9600 and stays silent for 5500 ms to expire any
previous session. A failed change recovers at 9600 without endless renegotiation.
The TMP68301 shared SPR stays 26; only SBRR2 changes. Physical adapter/clock
behavior must be verified independently from emulation. No 57600/115200 rates
are advertised. Linux PTYs require the private workspace's explicit baud bridge
when testing the emulated UART; termios alone does not change its clock.

## Controls and LED semantics

`metadata/controls.json` freezes catalog revision 1: 159 valid controls in a
184-ID space. Buttons and LEDs share IDs. Button bitmap byte is id/8, bit id%8
(least-significant bit first). Reserved IDs are not accepted as LED targets.
Duplicate IDs in a batch use the last pair. OFF=0, LOW=1, HIGH=2; monochrome LEDs
light for both LOW/HIGH. In each KY-307 bus the 12 LEDs share a color, so LOW and
HIGH cannot coexist within that bank; OFF is allowed. Validation includes
previous states of omitted IDs. Other groups can have independent colors.

Blinking is host policy, implemented by alternating ordinary output states.
Batches are logical transactions, not simultaneous electrical latch operations.
Button gesture policies (multibutton, double-click, hold) belong to the host.

## Snapshots and split analogs

Legacy snapshot is 55 bytes: time u32, button bitmap[23], raw T-bar u16,
raw X:u8/Y:u8 and encoder accumulators[6] as u32. Debounce is on the m68k;
the host compares consecutive snapshots. Encoder differences are signed modular
u32 subtraction; baseline sync produces no artificial movement. Physical 8-bit
encoder counters must be sampled before 128 counts can accumulate.

Each session begins in legacy mode. With capability 0080, ANALOG_MODE=1 replaces
STATE with reliable CONTROLS (51 bytes), while HELLO keeps the full baseline.
TBAR and JOYSTICK have independent u16 sequences including zero, no ACK/retries,
and sample time modulo 65536 ms. Modular sequence differences reject duplicates
and stale packets while accepting gaps. Only the latest sample is retained;
changes are sent with a 500 ms idle refresh. Buttons keep all queued edges;
encoders keep cumulative counts. Commands and reliable controls take priority.

Current minimum T-bar/joystick send intervals are 7/40 ms at ≥38400 baud,
16/80 ms at 19200, 32/160 ms at 9600. A sample is approximately 20 wire bytes.
These are scheduling limits, not guaranteed physical rates. Host calibration
rescales raw 12-bit T-bar and 8-bit joystick values without changing the protocol.

DIAGNOSTICS supplies time, link loop count, sample count, accepted UART TX bytes,
T-bar changes, X/Y changes, accumulated UART RX errors and maximum sample gap
(ms). It has its own u16 sequence and no ACK, approximately once per second.
Counter deltas use modular subtraction. Diagnostics do not acknowledge analog
reception. Application/version identification is requested once, not periodically.

## LCD character graphics (panel firmware 0.23)

The HD44780 supports eight simultaneous 5x8 CGRAM patterns, reused at any
character position. LCD_GLYPH loads one or more contiguous slots in an atomic validated batch;
each row is 0–31, bit 4 leftmost,
bit 0 rightmost. Row 0 is topmost. LCD_CELLS accepts slot codes 0–7 and ROM codes
32–255, including NUL as the valid slot-zero byte. Codes 8–31 are rejected; the
old ASCII commands remain unchanged. All arguments are validated before any
state change. ACK means queued, not physically displayed.

Upload glyphs before referencing their slots. Firmware services at most four
pixel-row writes per pass, restores DDRAM addressing afterwards, then services
text. Redefining a slot changes every visible occurrence of that slot. Slots
are global resources: menus and bitmap widgets must agree on ownership. A
multi-cell bitmap still has physical gaps between LCD character cells.

The host retains requested glyphs, reuploads after reconnect and compares the
acknowledged definition with desired state to avoid losing edits made in flight.
For older firmware it sends no new commands, substitutes `?` for unsupported
cells and avoids repeated repaint attempts. The protocol's new capability
indicates software support; physical CGRAM behavior requires a panel test.

### Panel ROM library (firmware 0.24)

LCD_ICONS loads library entries into consecutive CGRAM slots. Stable IDs 1–8
mean up, down, left, right, top-left, top-right, bottom-left, bottom-right.
Validate the entire list before modifying slots. These entries are stored in
application flash, not preallocated in CGRAM: all eight slots remain available
for custom uploads. Selection uses the same bounded LCD scheduler and is not
instantaneous at the hardware level. The host automatically uses this compact
command for matching arrow patterns when capability bit 10 is present; otherwise
it uploads their row bytes. More library IDs can be added without changing the
existing mappings; they must be coordinated with host definitions.

## kavtor manual transitions

The calibrated T-bar drives MIX and background WIPE directly. AUTO TRANS stays
off; the preview tally becomes HIGH while both background buses are on air.
MIX also follows armed NEXT TRANSITION keys. Manual WIPE with armed keys and
manual stingers are not supported yet and are rejected without starting a take.

Changing M/E freezes its unfinished take. Both T-bar direction indicators blink
until an endpoint is acquired in the new M/E. Returning to the original M/E
requires crossing its saved lever position to resume. Reaching the origin first
cancels at cut; reaching the opposite endpoint completes at cut. Intermediate
positions are coalesced at the network and rendering boundaries.

kavtor 0.7 adds `key_processing`: delegated LINEAR (89), CHR (91) and MAIN MASK
(100) operate independently per key/DSK. Detailed parameters live in kavtor
preparation; luma and separate key sources remain unsupported and beep.

kavtor 0.8 adds MIX keypad 1=MIX, 2=VFADE, 3=FADECUT, 4=CUTFADE.
DME 1–4 are PUSH left/right/top/bottom, 5–8 SLIDE in the same order (AUTO only).
These are local keypad slots, not invented Sony DME catalogue numbers.
`sonyWipes` selects the panel's Sony namespace; explicit web/API `smpte` requests
retain their original namespace. Unsupported DIRECT codes are rejected.

kavtor 0.17 keeps VFADE on MIX 2; DIP is MIX 5. With casparMIX 0.11,
NAM and SUPER MIX occupy 6 and 7. `mixPreparation` advertises DIP preparation;
`broadcastMixes` advertises NAM/SUPER MIX. State carries `dipColor`,
`superMixGainA`, `superMixGainB`; commands are `dip_color` and `mix_params`.
Both gains are integers 0–100. Preparation changes are saved by kavtor and
applied live only to TRANSITION PREVIEW. See the user guide for MODFY operation.
