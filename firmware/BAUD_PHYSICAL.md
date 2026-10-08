# Serial speed and physical acceptance

SCI2 starts at 9600 baud. Negotiated 38400-baud operation is validated on the
physical panel and is the supported development setting. Both controller and
panel must complete negotiation before exchanging frames at the new rate.
Disconnect/recovery returns to the startup rate.

76800 is deferred: Linux adapter rounding and physical negotiation did not
establish a reliable link. Do not advertise it as validated because a custom
termios divisor was accepted or an emulator ran at that rate.

Use an RS232 transceiver such as a suitable MAX3232 interface between panel
levels and the microcontroller UART. Logic-level GPIO is not an RS232 port.
Firmware install/update clients use their documented 9600-baud path.

Acceptance: boot at 9600, connect at preferred 38400, test buttons/LEDs/LCD and
analogue traffic, disconnect and reconnect at 9600, and verify recovery with
no continuous parser errors or unexpected resets. Record UART errors and scan
gaps rather than judging only a successful HELLO.
