# usbmidi
AtMega MIDI 1.0 Bridge/Echo, with GCC, Linux, ALSA, and JACK

The goals of this research
==========================

* Determine if regular USB-serial code can be used as MIDI bridge, using MIDI USB descriptors: **Done.**
* Conduct all required tests, to determine if this solution is adequate and fast enough to be reference code for latency (RTT) measurements, as well as for regular MIDI 1.0 bridge: **You decide if it is done.**


Target platform is **CJMCU Beetle** 16 MHz with hardware USB Full speed. But note that its resonator is ceramic, use with caution. To be useful as real 24h stage use MIDI bridge, we need to use quartz crystal in place of ceramic one, and, have exactly zero length USB cable (incl. enclosure internal USB cables). But even in this case nothing can be guaranteed, because of sensitive and stateful nature of USB bus itself.

USAGE
=====

Connections
-----------
* Add a button or Reed switch to `RST` and `GND` pins.
* _Optional_: Add 3-pin male header to `D9`, `D10`, `D11` pins, for mode jumper.
* _Optional_: Add MIDI duplex current loop transceiver (typical board with optocoupler and two DIN sockets) to `GND`, `5V`, `RX`, `TX` pins. 

Toolchain
---------

    # For Archlinux:
    pacman -S avr-gcc avr-libc
    # Should print:
    # This assembler was configured for a target of `avr'.
    $(avr-gcc -print-prog-name=as) --version

Compile
-------
1. Randomize `SERIALNUMBER` value at C code.
2. 

    avr-gcc -O2 -mmcu=atmega32u4 usbmidi.c && avr-objcopy -O binary a.out a.bin

3. Binary size expected like ~2,9 kb with `-O2`.

Load
----

CJMCU Beetle comes with bootloader. To enter it, plug in the module, activate RST button or Reed switch you just soldered in, then within a few seconds:

    avrdude -patmega32u4 -cavr109 -P/dev/ttyACM0 -b57600 -D -U flash:w:a.bin:r

Modes
-----

For bare board, with only Reset switch added, there is only **Hardware Echo** (loopback) mode, you will see _one_ bright blink after inserting board into USB.
If 3-pin header installed, you select two more modes: **regular MIDI bridge** at 31250 bps when `D10` to `D11` are shorted using jumper (before power on). There will be _two_ blinks. And `D9` to `D10` jumper is same but **38400 bps**, which allows to send and receive at serial terminal; there will be _three_ blinks.


Results
=======

Speed
-----

### 1. Self test

According to `usbmon`, with Echo (fastest possible) mode, **self** delay is below 100 us, typ. **70 us**, using `JACK2`, >15 yrs old CPU, 6.18.9 non-RT kernel, and low DSP load.

### 2. ALSA test

I will use `alsa-midi-latency-test`.

_Note_: Turn JACK server off for ALSA tests.

The commands are:

    alsa-midi-latency-test -l
    alsa-midi-latency-test -i 16:0 -o 16:0 -Rx2 -S 65536

And results are: median is **0.11 ms** with Echo, and **1.20 ms** with cable.

Note that cable transmission can't go below 3*10/31250=0.96 ms (for 3-byte message); here we see it's 1.09 ms. The difference is because we need to complex parse both incoming and outgoing traffic, due to USB MIDI 1.0 specs [1] are add extra headers (i.e. USB-MIDI is not possible as just USB-Serial directly). Using STM32 @ 72 MHz, i hope this extra 0.13 ms can be reduced.

Worst case i see is 3.22 ms with via cable, and 2.98 ms with Echo.

Using very poweful desktop, compared to this ancient CPU tests, gives almost no gain, during these _bare ALSA_ tests. There *is* difference with real life JACK setup tests. But these are in progress now.

### 3. JACK tests

TODO.

Known Bugs
==========

* At incomplete MIDI packet or wrong MIDI data (at MIDI IN), Activity LED remains to lit, until correct data. At other side, it is not only bug, but also feature, because it helps to detect some weird conditions like cable noise.
* There is no SysEx yet, this is planned.

RT Kernels
==========

There is no any gain using `linux-rt` i measure so far. Furthermore, JACK team also not recommend it for today's setups. _Btw, RT kernel gives bad effects on network audio, there will be separate research on it._

TODO
====

* Make it all in Assembler: WIP, with pre-requisites are ready (one complete C file, not depends on libraries, ~3 kb firmware).
* Use STM32 in place of Atmega, to gain even lower latency: Planned and depends on previous step.
* Add SysEx.

Thanks to
=========

1. https://medesign.seas.upenn.edu/index.php/Guides/MaEvArM-usb, M2 USB communication subsystem version: 2.3 date: March 21, 2013 authors: J. Fiene & J. Romano,  for bare metal (no lib, so most asm-ready) USB-Serial code. License: Unknown.
2. Objective Development Software GmbH, 2009, for free USB PID. https://github.com/robsoncouto/midikeyboard/tree/master/usbdrv  License: GNU GPL v2 or v3 at your choice.
3. morecat_lab, 2011, http://morecatlab.akiba.coocan.jp/morecat_lab/MocoLUFA.html, for MIDI 1.0 parsers. License: Creative Commons 2.5 Share/alike


License
=======

GNU GPL v2 or later.

References
==========

[1] https://www.usb.org/sites/default/files/midi10.pdf

[2] https://polyphonicexpression.com/midi-1-how-it-works

[3] https://midi.org/summary-of-midi-1-0-messages



