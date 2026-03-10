# usbmidi
AtMega MIDI Bridge/Echo, with GCC, Linux, ALSA, and JACK

The goals of this research
==========================

* Determine if regular USB-serial code can be used as MIDI bridge, using MIDI USB descriptors: **Done.**
* Conduct all required tests, to determine if this solution is adequate and fast enough to be reference code for MIDI 2.0/1.0 latency (RTT) measurements, as well as for regular MIDI 1.0 bridge: **You decide if it is done.**
* To just see MIDI 2.0 device in your Linux box, for a tiny bit of money and time. **Done.**


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
    pacman -S avr-gcc avr-libc avrdude
    # Should print:
    # This assembler was configured for a target of `avr'.
    $(avr-gcc -print-prog-name=as) --version

Compile
-------
1. Randomize `SERIALNUMBER` value at C code.
2. ```avr-gcc -O2 -mmcu=atmega32u4 usbmidi.c && avr-objcopy -O binary a.out a.bin```
3. Binary size expected like ~3,3 kb with `-O2`.

Load
----

CJMCU Beetle comes with [bootloader](https://github.com/adafruit/Caterina-Bootloader). To enter it, plug in the module, activate RST button or Reed switch you just soldered in, then within a few seconds:

    avrdude -patmega32u4 -cavr109 -P/dev/ttyACM0 -b57600 -D -U flash:w:a.bin:r

Midi 2.0-1.0 switch
----------------------
As per [midi.org](https://midi.org/building-a-usb-midi-2-0-device-part-3) [4], here we have two Alternate Settings for MIDI 1.0 and 2.0. Unlike some other projects and musical instruments, there is no hardware switch. Linux kernel and Sound driver are responsible to select best mode via USB altset, and today it is MIDI 2.0. 

To fall back to regular MIDI 1.0:
* stop JACK Server;
* unplug board;
* and:
```
    rmmod snd-usb-audio
    or 
    rmmod -f snd-usb-audio  # Use Force with caution.
    modprobe snd-usb-audio midi2_enable=0
```    
When we inited as MIDI 2.0, you will see _one extra small blink_ of Activity LED, along with bright flashes outlined below: it is MIDI 2.0 status packet from driver. And, look at `dmesg`, of course.

Note that with Midi 2.0, only Echo mode is fully operational. Midi 2.0 messages should not be passed through cable. While code supports it and technically will work, there is nonsense to use it as other than informational tests, cable loopback (note increase RTT for 8 bytes, compared to three, @ 31250 bps) or feed to/from serial terminal. Or, MIDI 2.0<->1.0 translation need. But i do not want to reinvent the bike, because your Linux PC already have this translation inside, and it is used after switch to MIDI 1.0 using command above.

As a side note, one may note that, using Midi 2.0, there is no way to: tell the OS/driver that we prefer MIDI 1.0; or, to have both MIDI 1.0 and 2.0 at same time (like each at its own endpoint), sadly. At least, Midi.org is not defines these conditions. So, if one need to avoid commands above by adding hardware switch to force MIDI 1.0, one will need to switch to pure old MIDI 1.0 USB descriptor (i keep it in code for this purpose), not the MIDI 1.0 half of two-way descriptor, as the latter is impossible from our side, and this is a bit weird.

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

Worst case i see is **3.7 ms** with via cable, and **2.98 ms** with Echo.

Using very poweful desktop, compared to this ancient CPU tests, gives almost no gain, during these _bare ALSA_ tests. There *is* difference with real life JACK setup tests. But these are in progress now.

For _MIDI 2.0_, i measure _median_ RTT is **0.09 ms** with Echo, which is 20 us better than _MIDI 1.0_, this was unexpected. This can mean that no compatibility translation occurs in kernel or driver in this case, so we have not worse, but better RTT, with larger wire payload.

What is even more unexpected, for _MIDI 2.0_ i see _worst case_ RTT like an **order** of improvement, it is **0.31 ms** with Echo. This can means that translation occurs somewhere in user space. So, _MIDI 2.0_ not only cut delay a bit, but also greatly reduces jitter. However, it can be less different on powerful CPU.


### 3. JACK tests

TODO.

Known Bugs
==========

* At incomplete MIDI packet or wrong MIDI data (at MIDI IN), Activity LED remains to lit, until correct data. At other side, it is not only bug, but more like a feature, because it helps to detect some weird conditions like cable noise.
Example is low byte like `0x55` not preambled with high (status) byte like `0x90`. Other example is unfinished `0xf0, 0x55, 0x55`... sysex.
  

RT Kernels
==========

There is no any gain using `linux-rt` i measure so far. Furthermore, JACK team also not recommend it for today's setups. _Btw, RT kernel gives bad effects on network audio, there will be separate research on it._

Current MIDI 2.0 status
=======================
It turns out that not so much we can do with pure MIDI 2.0. Most tests will show things are "work", but it is due to kernel/driver transtation from/to MIDI 1.0, so only we can see are 3-byte packets.

Here we can see pure MIDI 2.0:

    hexdump -Cv /dev/snd/umpC0D0

But here are only 1.0 3-byte values, decimated by system:

    amidi -d -p hw:0,1,0

And here i am still can't decide what exactly i receive, as no raw dump:

    aseqdump -u 2 -r -R -p 16:1    

As for JACK and its apps ecosystem, there is no MIDI2 support, and not planned, as for Mar, 2026. There are requests [7] and tools [8] [9] that shows that current JACK is technically works, but can't be used in real setup, as we will see below:

To compile these tools, namely, `jacktestumpsender` and `jackumptestmonitor`:

    cd Sources/
    g++ -Wall *.cpp -g -O2 -s -ljack `wx-config --cflags` `wx-config --libs`  

Then we see tools itself are works with MIDI 2.0.

But trying to see what we receive from USB, our 8-byte packets are displayed at receiver as 3-byte MIDI 1.0 messages.

TODO
====

* Make it all in Assembler: WIP, with pre-requisites are ready (one complete C file, not depends on libraries, ~3 kb firmware, and, there are working USB-Serials for Atmega in Assembler, that's why it is important to base this code on USB-Serial, not on "real" USB-Midi).
* Use STM32 in place of Atmega, to gain even lower latency: Planned and depends on previous step.

See also
========
`ttymidi`. It is one-direction bridge, Serial -> ALSA MIDI.
It will work with our bridge, when it at **Mode 38400**, like

    ttymidi -s /dev/ttyS0 -b 38400

Sadly, `alsa-midi-latency-test -l` not show its port 129, but `aconnect -i` exposes it, and

    alsa-midi-latency-test -i 129:0 -o 16:0 -Rx2

will work.


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

[4] https://midi.org/building-a-usb-midi-2-0-device-part-3

[5] https://cmtext.com/MIDI/chapter3_channel_voice_messages.php

[6] https://allthings.how/how-to-enable-midi-2-0-on-linux/

[7] https://github.com/jackaudio/jack2/issues/535

[8] https://github.com/bbouchez/jacktestumpsender

[9] https://github.com/bbouchez/jackumptestmonitor
