# Independent analogue streams

T-bar and joystick updates are separate from button/encoder events. Changed
values are sent independently, with periodic refresh when idle. UART TX uses
interrupt-driven buffering; the event loop does not repeatedly block on LCD
work or duplicate the entire input state after every change.

The T-bar reports a 12-bit value. Its physical end stops need calibration;
extra pressure near an end can cross the raw wrapping boundary. Joystick axes
are independent 8-bit readings with calibrated centre, limits and dead zone.
Do not infer the range from a single observed sweep or confuse a calibrated
value with a raw ADC sample.

The controller stores calibration outside source control. Use its web/LCD
configuration and analogue diagnostics to observe raw/calibrated values and
receive rates. Firmware reports scan/loop rates, changes, UART errors and
maximum scan gaps; controller diagnostics report independent stream rates and
gaps. A quiet control naturally emits few changed-value samples.

Physical development measurements reached approximately 200 scans/second.
Those observations do not guarantee a changed packet for every scan or a
particular mixer output rate. Measure continuous movement and link contention
with the chosen hardware. `make -C firmware test-analog test-analog-acquire`
provides local arithmetic/acquisition checks, not hardware calibration proof.
