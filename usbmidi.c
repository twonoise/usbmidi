
// avr-gcc -O2 -mmcu=atmega32u4 usbmidi.c && avr-objcopy -O binary a.out a.bin && avrdude -patmega32u4 -cavr109 -P/dev/ttyACM0 -b57600 -D -U flash:w:a.bin:r

// rmmod -f snd-usb-audio && modprobe snd-usb-audio midi2_enable=0

/*
USB FS to MIDI 1.0 Bridge & MIDI 2.0/1.0 no-cable Echo (loopback), based on fast USB-Serial, for RTT measure and regular use.
Platform: CJMCU Beetle 16 MHz with h/w USB FS, but note its resonator is ceramic, use with caution.
Binary:   ~3 kb with -O2.
Speed:    According to usbmon, self delay is below 100 us, typ. 65 us, using JACK2, >15 yrs old CPU, 6.18.9 non-RT kernel, and low DSP load. Both MIDI1 & MIDI2.
License:  GNU GPL v2 or later.

Thanks to:
1. https://medesign.seas.upenn.edu/index.php/Guides/MaEvArM-usb, M2 USB communication subsystem version: 2.3 date: March 21, 2013 authors: J. Fiene & J. Romano,  for bare metal (no lib, so most asm-ready) USB-Serial code. License: Unknown.
2. Objective Development Software GmbH, 2009, for free USB PID. https://github.com/robsoncouto/midikeyboard/tree/master/usbdrv  License: GNU GPL v2 or v3 at your choice.
3. morecat_lab, 2011, http://morecatlab.akiba.coocan.jp/morecat_lab/MocoLUFA.html, for MIDI 1.0 parsers. License: Creative Commons 2.5 Share/alike
4. https://midi2-dev.github.io/usbMIDI2DescriptorBuilder/ for MIDI 2.0 descriptor. License: Unknown.
*/

#define F_CPU 16000000UL

#define ASCII_MANUFACTURER   L"TwoNoise"
#define ASCII_PRODUCT        L"MIDI 2.0 Bridge"
#define ASCII_SERIALNUMBER   L"38198562"  // MUST HAVE as per USB-IDs-for-free.txt
#define ASCII_NAME           L"MIDIBridge"
#define VID                  0xc0, 0x16
#define PID                  0xde, 0x27

#include <stdlib.h>
#include <string.h>
#include <util/delay_basic.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#define EP_SIZE(s)         ((s)==64 ? 0x30 : ((s)==32 ? 0x20 : ((s)==16 ? 0x10 : 0x00)))
#define cbi(sfr, bit)      ((sfr) &= ~(1 << (bit)))
#define sbi(sfr, bit)      ((sfr) |= (1 << (bit)))

// Configurable Options
#define TRANSMIT_TIMEOUT        25  // ms
#define SUPPORT_ENDPOINT_HALT

// Endpoint Buffer Configuration
#define EP0SZ  8
#define RXEP   1
#define TXEP   2
#define RXSZ   64
#define TXSZ   64

 const uint8_t PROGMEM endpoint_config_table[] = {
    1, 0x81, EP_SIZE(TXSZ) | 0x06, // EP_TYPE_BULK_IN, EP_DOUBLE_BUFFER
    1, 0x80, EP_SIZE(RXSZ) | 0x06, // EP_TYPE_BULK_OUT, EP_DOUBLE_BUFFER
};

 const uint8_t PROGMEM device_descriptor[] = {
    18, 1, 0x10, 0x01, 0xef, 2, 1, EP0SZ, VID, PID, 0, 1, 1, 2, 3, 1,
};

 const uint8_t PROGMEM config1_descriptor[] = {
    // MIDI 1.0
    // 9, 2, 0x65, 0, 2, 1, 0, 0xC0, 50, // 9+9+9+9+7+6+6+9+9+9+5+9+5 = 0x0065
    // 9, 4, 0, 0, 0, 1, 1, 0, 0,
    // 9, 0x24, 1, 0, 1, 0x09, 0, 1, 1,
    // 9, 4, 1, 0, 2, 1, 3, 0, 0,
    // 7, 0x24, 1, 0, 1, 0x41, 0,  // 7+6+6+9+9+9+5+9+5 = 0x0041
    //
    // 6, 0x24, 2, 1, 1, 0,
    // 6, 0x24, 2, 2, 2, 0,
    // 9, 0x24, 3, 1, 3, 1, 2, 1, 0,
    // 9, 0x24, 3, 2, 4, 1, 1, 1, 0,
    //
    // 9, 5, TXEP, 0x02, TXSZ, 0, 1, 0, 0,
    // 5, 0x25, 1, 1, 1,
    // 9, 5, RXEP | 0x80, 0x02, RXSZ, 0, 1, 0, 0,
    // 5, 0x25, 1, 1, 3,

    // MIDI 2.0+1.0 via altset
    9, 2, 0x95, 0, 2, 1, 0, 0x80, 0xFA,
    8, 0x0B, 0, 2, 1, 3, 0, 0,
    9, 4, 0, 0, 0, 1, 1, 0, 0,
    9, 0x24, 1, 0, 1, 9, 0, 1, 1,
    9, 4, 1, 0, 2, 1, 3, 0, 2,
    7, 0x24, 1, 0, 1, 0x41, 0,
    6, 0x24, 2, 1, 1, 4,
    9, 0x24, 3, 2, 1, 1, 1, 1, 4,
    6, 0x24, 2, 2, 2, 4,
    9, 0x24, 3, 1, 0x12, 1, 0x12, 1, 4,
    9, 5, TXEP, 2, 0x40, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, RXEP | 0x80, 2, 0x40, 0, 0, 0, 0,
    5, 0x25, 1, 1, 0x12,

    9, 4, 1, 1, 2, 1, 3, 0, 2,  // 9, 4, 1, 1, 2, ... // 9, 4, 1, 0, 2, ...
    7, 0x24, 1, 0, 2, 7, 0,
    7, 5, TXEP, 2, 0x40, 0, 0,
    5, 0x25, 2, 1, 1,
    7, 5, RXEP | 0x80, 2, 0x40, 0, 0,
    5, 0x25, 2, 1, 1,
};

 const uint8_t PROGMEM device_descriptor_qualifier[] = {
    0x0A, 6, 0, 2, 0xEF, 2, 1, 0x40, 1, 0,
};
 const uint8_t PROGMEM gtb0[] = {
    5, 0x26, 1, 0x12, 0, 0x0D, 0x26, 2, 1, 0, 0, 1, 4, 0x11, 0, 0, 0, 0,
};

struct usb_string_descriptor_struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    int16_t wString[];
};
 const struct usb_string_descriptor_struct PROGMEM string0 = {
    4, 3, {0x0409}
};
 const struct usb_string_descriptor_struct PROGMEM string1 = {
    sizeof(ASCII_MANUFACTURER), 3, ASCII_MANUFACTURER
};
 const struct usb_string_descriptor_struct PROGMEM string2 = {
    sizeof(ASCII_PRODUCT),      3, ASCII_PRODUCT
};
 const struct usb_string_descriptor_struct PROGMEM string3 = {
    sizeof(ASCII_SERIALNUMBER), 3, ASCII_SERIALNUMBER
};
 const struct usb_string_descriptor_struct PROGMEM string4 = {
    sizeof(ASCII_NAME), 3, ASCII_NAME
};

// This table defines which descriptor data is sent for each specific
// request from the host (in wValue and wIndex).
 const struct descriptor_list_struct {
    uint16_t wValue;
    uint16_t wIndex;
    const uint8_t *addr;
    uint8_t  length;
} PROGMEM descriptor_list[] = {
    {0x0100, 0x0000+0, device_descriptor, sizeof(device_descriptor)},
    {0x0200, 0x0000+0, config1_descriptor, sizeof(config1_descriptor)},
    {0x0300, 0x0000+0, (const uint8_t *)&string0, 4},
    {0x0301, 0x0409+0, (const uint8_t *)&string1, sizeof(ASCII_MANUFACTURER)},
    {0x0302, 0x0409+0, (const uint8_t *)&string2, sizeof(ASCII_PRODUCT)},
    {0x0303, 0x0409+0, (const uint8_t *)&string3, sizeof(ASCII_SERIALNUMBER)},
    {0x0304, 0x0409+0, (const uint8_t *)&string4, sizeof(ASCII_NAME)},
    {0x0600, 0x0000+0, device_descriptor_qualifier, sizeof(device_descriptor_qualifier)},
    {0x2601, 0x0001+0, gtb0, sizeof(gtb0)},
};
#define NUM_DESC_LIST (sizeof(descriptor_list)/sizeof(struct descriptor_list_struct))

// For USB->MIDI, we need larger size,
// as it is speed conversion bottleneck.
// Also used for Echo.
uint8_t ringbuf[256];
uint8_t uwptr, irptr;
// We need two independent buffers, due to full duplex.
// Is our RAM enough?
// For MIDI1/2->USB. FIXME Will it work for large SysEx's?
uint8_t buf[8];

uint8_t usb_isconfigured;
uint8_t transmit_previous_timeout;
uint8_t altset;
uint8_t p, q;

// number of bytes available in the receive buffer
unsigned char usb_rx_available(void)
{
    uint8_t n, intr_state;

    n = 0;

    intr_state = SREG;
    cli();
    if (usb_isconfigured) {
        UENUM = TXEP;
        n = UEBCLX;
    }
    SREG = intr_state;
    return (unsigned char)n;
}

// get the next character, or -1 if nothing received
char usb_rx_char(void)
{
    uint8_t c, intr_state;

// interrupts are disabled so these functions can be
// used from the main program or interrupt context,
// even both in the same program!
    intr_state = SREG;
    cli();
    if (!usb_isconfigured) {
        SREG = intr_state;
        return -1;
    }
    UENUM = TXEP;
    if (!(UEINTX & (1<<RWAL))) {
// no data in buffer
        SREG = intr_state;
        return -1;
    }
    // take one byte out of the buffer
    c = UEDATX;
    // if buffer completely used, release it
    if (!(UEINTX & (1<<RWAL))) UEINTX = 0x6B;
    SREG = intr_state;
    return (char)c;
}

// transmit a buffer.
//  0 returned on success, -1 on error
// This function is optimized for speed!  Each call takes approx 6.1 us overhead
// plus 0.25 us per byte.  12 Mbit/sec USB has 8.67 us per-packet overhead and
// takes 0.67 us per byte.  If called with 64 byte packet-size blocks, this function
// can transmit at full USB speed using 43% CPU time.  The maximum theoretical speed
// is 19 packets per USB frame, or 1216 kbytes/sec.  However, bulk endpoints have the
// lowest priority, so any other USB devices will likely reduce the speed.  Speed
// can also be limited by how quickly the PC-based software reads data, as the host
// controller in the PC will not allocate bandwitdh without a pending read request.
// (thanks to Victor Suarez for testing and feedback and initial code)
int8_t usb_tx_buffer(const uint8_t *buffer, uint16_t size)
{
    uint8_t timeout, intr_state, write_size;

    // if we're not online (enumerated and configured), error
    if (!usb_isconfigured)
        return -1;
    // interrupts are disabled so these functions can be
    // used from the main program or interrupt context,
    // even both in the same program!
    intr_state = SREG;
    cli();
    UENUM = RXEP;
    // if we gave up due to timeout before, don't wait again
    if (transmit_previous_timeout) {
        if (!(UEINTX & (1<<RWAL))) {
            SREG = intr_state;
            return -1;
        }
        transmit_previous_timeout = 0;
    }
    // each iteration of this loop transmits a packet
    while (size) {
        // wait for the FIFO to be ready to accept data
        timeout = UDFNUML + TRANSMIT_TIMEOUT;
        while (1) {
            // are we ready to transmit?
            if (UEINTX & (1<<RWAL))
                break;
            SREG = intr_state;
            // have we waited too long?  This happens if the user
            // is not running an application that is listening
            if (UDFNUML == timeout) {
                transmit_previous_timeout = 1;
                return -1;
            }
            // has the USB gone offline?
            if (!usb_isconfigured)
                return -1;
            // get ready to try checking again
            intr_state = SREG;
            cli();
            UENUM = RXEP;
        }

        // compute how many bytes will fit into the next packet
        write_size = TXSZ - UEBCLX;
        if (write_size > size)
            write_size = size;
        size -= write_size;

        // write the packet
        switch (write_size) {
            #if (TXSZ == 64)
            case 64: UEDATX = *buffer++;
            case 63: UEDATX = *buffer++;
            case 62: UEDATX = *buffer++;
            case 61: UEDATX = *buffer++;
            case 60: UEDATX = *buffer++;
            case 59: UEDATX = *buffer++;
            case 58: UEDATX = *buffer++;
            case 57: UEDATX = *buffer++;
            case 56: UEDATX = *buffer++;
            case 55: UEDATX = *buffer++;
            case 54: UEDATX = *buffer++;
            case 53: UEDATX = *buffer++;
            case 52: UEDATX = *buffer++;
            case 51: UEDATX = *buffer++;
            case 50: UEDATX = *buffer++;
            case 49: UEDATX = *buffer++;
            case 48: UEDATX = *buffer++;
            case 47: UEDATX = *buffer++;
            case 46: UEDATX = *buffer++;
            case 45: UEDATX = *buffer++;
            case 44: UEDATX = *buffer++;
            case 43: UEDATX = *buffer++;
            case 42: UEDATX = *buffer++;
            case 41: UEDATX = *buffer++;
            case 40: UEDATX = *buffer++;
            case 39: UEDATX = *buffer++;
            case 38: UEDATX = *buffer++;
            case 37: UEDATX = *buffer++;
            case 36: UEDATX = *buffer++;
            case 35: UEDATX = *buffer++;
            case 34: UEDATX = *buffer++;
            case 33: UEDATX = *buffer++;
            #endif
            #if (TXSZ >= 32)
            case 32: UEDATX = *buffer++;
            case 31: UEDATX = *buffer++;
            case 30: UEDATX = *buffer++;
            case 29: UEDATX = *buffer++;
            case 28: UEDATX = *buffer++;
            case 27: UEDATX = *buffer++;
            case 26: UEDATX = *buffer++;
            case 25: UEDATX = *buffer++;
            case 24: UEDATX = *buffer++;
            case 23: UEDATX = *buffer++;
            case 22: UEDATX = *buffer++;
            case 21: UEDATX = *buffer++;
            case 20: UEDATX = *buffer++;
            case 19: UEDATX = *buffer++;
            case 18: UEDATX = *buffer++;
            case 17: UEDATX = *buffer++;
            #endif
            #if (TXSZ >= 16)
            case 16: UEDATX = *buffer++;
            case 15: UEDATX = *buffer++;
            case 14: UEDATX = *buffer++;
            case 13: UEDATX = *buffer++;
            case 12: UEDATX = *buffer++;
            case 11: UEDATX = *buffer++;
            case 10: UEDATX = *buffer++;
            case  9: UEDATX = *buffer++;
            #endif
            case  8: UEDATX = *buffer++;
            case  7: UEDATX = *buffer++;
            case  6: UEDATX = *buffer++;
            case  5: UEDATX = *buffer++;
            case  4: UEDATX = *buffer++;
            case  3: UEDATX = *buffer++;
            case  2: UEDATX = *buffer++;
            default:
            case  1: UEDATX = *buffer++;
            case  0: break;
        }
        // if this completed a packet, transmit it now!
        if (!(UEINTX & (1<<RWAL)))
            UEINTX = 0x3A;
        // transmit_flush_timer = TRANSMIT_FLUSH_TIMEOUT;
    }
    SREG = intr_state;
    return 0;
}

// immediately transmit any buffered output.
// This doesn't actually transmit the data - that is impossible!
// USB devices only transmit when the host allows, so the best
// we can do is release the FIFO buffer for when the host wants it
void usb_tx_push(void)
{
    uint8_t intr_state;

    intr_state = SREG;
    cli();
    // if (transmit_flush_timer) {
        UENUM = RXEP;
        UEINTX = 0x3A;
        // transmit_flush_timer = 0;
    // }
    SREG = intr_state;
}


// USB Device Interrupt - handle all device-level events
// the transmit buffer flushing is triggered by the start of frame
ISR(USB_GEN_vect)
{
    uint8_t intbits, t;

    intbits = UDINT;
    UDINT = 0;
    if (intbits & (1<<EORSTI)) {
        UENUM = 0;
        UECONX = 1;
        UECFG0X = 0;
        UECFG1X = EP_SIZE(EP0SZ) | 0x02; // EP_SINGLE_BUFFER
        UEIENX = (1<<RXSTPE);
        usb_isconfigured = 0;
    }
}

// Misc functions to wait for ready and send/receive packets
 inline void usb_wait_in_ready(void)
{
    while (!(UEINTX & (1<<TXINI))) ;
}
 inline void usb_send_in(void)
{
    UEINTX = ~(1<<TXINI);
}

void blink(uint8_t n)
{
    while(n)
    {
        sbi (PORTC, 7);
        for(uint8_t i=0;i<10;i++)
            _delay_loop_2 ((uint16_t)65535);
        cbi (PORTC, 7);
        for(uint8_t i=0;i<10;i++)
            _delay_loop_2 ((uint16_t)65535);
        if (n < 255)
            n--;
    }
}

// USB Endpoint Interrupt - endpoint 0 is handled here.  The
// other endpoints are manipulated by the user-callable
// functions, and the start-of-frame interrupt.
ISR(USB_COM_vect)
{
    uint8_t intbits;
    const uint8_t *list;
    const uint8_t *cfg;
    uint8_t i, n, len, en;
    // uint8_t *p;
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint16_t desc_val;
    const uint8_t *desc_addr;
    uint8_t desc_length;

    UENUM = 0;
    intbits = UEINTX;
    if (intbits & (1<<RXSTPI)) {
        bmRequestType = UEDATX;
        bRequest = UEDATX;
        wValue = UEDATX;
        wValue |= (UEDATX << 8);
        wIndex = UEDATX;
        wIndex |= (UEDATX << 8);
        wLength = UEDATX;
        wLength |= (UEDATX << 8);
        UEINTX = ~((1<<RXSTPI) | (1<<RXOUTI) | (1<<TXINI));

        if (bRequest == 6) { // GET_DESCRIPTOR
            list = (const uint8_t *)descriptor_list;
            for (i=0; ; i++) {
                if (i >= NUM_DESC_LIST) {
                    UECONX = (1<<STALLRQ)|(1<<EPEN);  //stall
                    return;
                }
                desc_val = pgm_read_word(list);
                if (desc_val != wValue) {
                    list += sizeof(struct descriptor_list_struct);
                    continue;
                }
                list += 2;
                desc_val = pgm_read_word(list);
                if (desc_val != wIndex) {
                    list += sizeof(struct descriptor_list_struct)-2;
                    continue;
                }
                list += 2;
                desc_addr = (const uint8_t *)pgm_read_word(list);
                list += 2;
                desc_length = pgm_read_byte(list);
                break;
            }
            len = (wLength < 256) ? wLength : 255;
            if (len > desc_length) len = desc_length;
            do {
                // wait for host ready for IN packet
                do {
                    i = UEINTX;
                } while (!(i & ((1<<TXINI)|(1<<RXOUTI))));
                if (i & (1<<RXOUTI))
                    return;    // abort
                // send IN packet
                n = len < EP0SZ ? len : EP0SZ;
                for (i = n; i; i--) {
                    UEDATX = pgm_read_byte(desc_addr++);
                }
                len -= n;
                usb_send_in();
            } while (len || n == EP0SZ);
            return;
        }
        if (bRequest == 5) { // SET_ADDRESS
            usb_send_in();
            usb_wait_in_ready();
            UDADDR = wValue | (1<<ADDEN);
            return;
        }
        if (bRequest == 11) { // REQ_SET_INTERFACE
            usb_send_in();
            altset = wValue & (uint16_t)1;
            // PC = 0;
            p = 6;
            q = 0;
            return;
        }
        if (bRequest == 9 && bmRequestType == 0) { // SET_CONFIGURATION
            usb_send_in();
            usb_isconfigured = wValue;
            // transmit_flush_timer = 0;
            cfg = endpoint_config_table;
            for (i=1; i<3; i++) {
                UENUM = i;
                en = pgm_read_byte(cfg++);
                UECONX = en;
                if (en) {
                    UECFG0X = pgm_read_byte(cfg++);
                    UECFG1X = pgm_read_byte(cfg++);
                }
            }
            UERST = 0x1E;
            UERST = 0;
            return;
        }
        if (bRequest == 8 && bmRequestType == 0x80) { // GET_CONFIGURATION
            usb_wait_in_ready();
            UEDATX = usb_isconfigured;
            usb_send_in();
            return;
        }

        if (bRequest == 0) { // GET_STATUS
            usb_wait_in_ready();
            i = 0;
#ifdef SUPPORT_ENDPOINT_HALT
            if (bmRequestType == 0x82) {
                UENUM = wIndex;
                if (UECONX & (1<<STALLRQ)) i = 1;
                UENUM = 0;
            }
#endif
            UEDATX = i;
            UEDATX = 0;
            usb_send_in();
            return;
        }
#ifdef SUPPORT_ENDPOINT_HALT
        if ((bRequest == 1 || bRequest == 3) // CLEAR_FEATURE || SET_FEATURE
          && bmRequestType == 0x02 && wValue == 0) {
            i = wIndex & 0x7F;
            if (i >= 1 && i <= 2) { // MAX_ENDPOINT
                usb_send_in();
                UENUM = i;
                if (bRequest == 3) {
                    UECONX = (1<<STALLRQ)|(1<<EPEN);
                } else {
                    UECONX = (1<<STALLRQC)|(1<<RSTDT)|(1<<EPEN);
                    UERST = (1 << i);
                    UERST = 0;
                }
                return;
            }
        }
#endif
    }
    UECONX = (1<<STALLRQ) | (1<<EPEN);      // stall
}


int main(void)
{
    sbi(DDRC, 7);  // CJMCU LED, pin 32 PC7

    sbi(DDRB,  6);  // CJMCU "D10": out 0 (gnd)
    cbi(PORTB, 6);  // CJMCU "D10": out 0 (gnd)
    cbi(DDRB,  5);  // CJMCU "D9": in + pullup
    sbi(PORTB, 5);  // CJMCU "D9": in + pullup
    cbi(DDRB,  7);  // CJMCU "D11": in + pullup
    sbi(PORTB, 7);  // CJMCU "D11": in + pullup

    usb_isconfigured = 0;
    transmit_previous_timeout = 0;
    uwptr = 0;
    irptr = 0;
    altset = 0;
    p = 6;
    q = 0;

    uint16_t n = 0;

    UHWCON = 0x01;
    USBCON = ((1<<USBE)|(1<<FRZCLK));
    PLLCSR = 0x12;                     // 0001 0010 For a 16MHz clock
    while (!(PLLCSR & (1<<PLOCK))) ;
    USBCON = ((1<<USBE)|(1<<OTGPADE));
    UDCON = 0;                         // ~(1 << DETACH), Full speed
    usb_isconfigured = 0;
    UDIEN = (1<<EORSTE); // |(1<<SOFE);
    sei();

    while(usb_isconfigured == 0) { // wait for a connection
        sbi(PORTC, 7);
        _delay_loop_2 ((uint16_t)8191);
        cbi(PORTC, 7);
        _delay_loop_2 ((uint16_t)65535);
    };

    uint8_t jumper = PINB & 0b10100000;

    if (jumper == 0b10100000) { // No jumper
        blink(1);

        // MIDI 1.0/2.0 Echo.
        // Message length is 4/8 bytes max, except Sysex which are unlimited length.
        // https://polyphonicexpression.com/midi-1-how-it-works
        // https://midi.org/summary-of-midi-1-0-messages
        // I try to support long messages here.
        // Looks like here we do not need to differ MIDI 1.0 from 2.0.
        while(1)
        {
            if (usb_rx_available() == 0)
                continue;
            sbi(PORTC, 7);
            goto skip;

            while(usb_rx_available())
            {
skip:
                // ringbuf used here as ordinary buf.
                // TODO Here we may not need any byffering,
                // try to just pass incoming USB to outgoing immegiately
                // (64-byte pieces).
                ringbuf[n++] = usb_rx_char();
                if (n == 256)
                    break;
            }
            usb_tx_buffer (ringbuf, n);
            usb_tx_push();
            n = 0;
            cbi(PORTC, 7);
        }
    }

    // Here is jumper present, so we need USART.
    // Speed is selectable: for MIDI, or for serial terminal debug.
    UCSR1A = (1 << UDRE1) ;                 // U2X1 = 0
    if (jumper == 0b00100000)
    {                                       // Jumper "D10"-"D11"
        blink(2);
        UBRR1 = (F_CPU / 16 / 31250) - 1;   // Exact speed
    }
    else
    {                                       // Jumper "D10"-"D9"
        blink(3);
        UBRR1 = (F_CPU / 16 / 38400) - 1;   // 1% speed error is "ok"
    }
    UCSR1B = (1 << RXEN1) | (1 << TXEN1);   // No interrupts
    UCSR1C = (3 << UCSZ10);                 // 8-N-1

    // MIDI to USB, and vice versa (duplex).
    while(1)
    {
        // Unlike of Echo mode, for Bridge, we differ MIDI 1.0 from 2.0.
        // Check if we are asked to switch alternate setting from host OS/driver.
        // This can be happened at any time.

        if (altset)
        // MIDI 2.0
        // This is informational only, like display data on serial terminal.
        // Do not feed it to input of actual MIDI hardware.
        // If you need that, a complex parser 2.0 <-> 1.0 messages would be need,
        // as MIDI 2.0 can't be used via MIDI cable. But your Linux system
        // already "converts these MIDI 1.0 byte stream messages
        // to UMP format between the Device and the application" as
        // per midi.org, so create yet another (slow) parser is not
        // adequate: just use MIDI 1.0 mode we have as altset (again,
        // as per midi.org).
        {
            // Phase 1. Read MIDI IN serial bytes, if any;
            //          create USB packet, and send it.
            if(UCSR1A & (1<<RXC1)) {
                // TODO add timeout for LED off, if MIDI RX terminated in middle.
                sbi(PORTC, 7);
                buf[q++] = UDR1;
                if (q == 8)
                {
                    q = 0;
                    usb_tx_buffer (buf, 8);
                    usb_tx_push();
                    cbi(PORTC, 7);
                }
            }

            // Phase 2. Check if USB packet received, get bytes and emit on MIDI OUT.
            while(usb_rx_available())
            {
                sbi(PORTC, 7); // TODO Make it to fire only once, as at Echo.

                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
                ringbuf[uwptr++] = usb_rx_char();
            }

            if( (UCSR1A & (1<<UDRE1)) && uwptr!=irptr ) {
                UDR1 = ringbuf[irptr++];
                // irptr &= TX_MASK; // We have exactly one byte size pointers.
            }

            // Wait for actual end of serial transmission, to correctly turn LED off.
            // Note that if no one byte was transmitted, like thrown away packet,
            // this flag not fires, and LED continues to lit until valid transmission.
            // It can help to detect errors.
            // Or, turn on 'cbi...' above at throw away section.
            if(UCSR1A & (1<<TXC1)) {
                sbi(UCSR1A, TXC1); // Clear this flag, to fire only once.
                cbi(PORTC, 7);
            }
        }
        else
        { // MIDI 1.0
            // Phase 1. Read MIDI IN serial bytes, if any;
            //          create USB packet, and send it.
            if(UCSR1A & (1<<RXC1)) {
                // TODO add timeout for LED off, if MIDI RX terminated in middle.
                sbi(PORTC, 7);

                uint8_t RxByte = UDR1;

                // Remove incoming MIDI bytes waiting timeout at any cost:
                // We can just pass bytes to USB, but to create packet,
                // it is impossible without timeout, because serial MIDI
                // receive is async, and gaps between bytes are not known
                // and will vary. So this complex state machine used.

                if (RxByte == 0xF7) // End of SysEx
                {
                    if (q == 4)     // It's currently SysEx
                    {
                        q = 0;
                        n = 0;
                        buf[0] = p;
                        if (p == 5)
                            goto utxrdy2;
                        else if (p == 6)
                            goto utxrdy3;
                        else // 7
                            goto utxrdy4;
                    }
                    q = 0;
                    goto stop;
                }

                // 1-byte commands.
                // Note that 0xF4 and 0xF5 are undefined/reserved, so, while
                // we pass it correctly (as single-bytes), still they can be
                // thrown away, depending on kernel USB-MIDI driver, as their
                // length is also undefined officially.
                if (RxByte >= 0xF4)
                    goto utxrdy1;
                else if (RxByte > 0x7F) // 0x80..0xF3
                {
                    p = 6;
                    q = 3;

                    // This status lasts until it will be replaced with new one.
                    // I.e. chord is not 0x90 0x40 0x7f, 0x90 0x41 0x7f...,
                    // but 0x90 0x40 0x7f 0x41 0x7f... on MIDI cable.
                    buf[1] = RxByte;

                    // Payload bytes for this status byte: 1, 2, or 3 for sysex.
                    // Zero-payload status bytes are handled earlier.

                    if (RxByte < 0xF0) // 0x80..0xEF
                    {
                        buf[0] = RxByte >> 4;  // Table 4-1
                        // 0xCx Program Change, 0xDx Channel Pressure
                        if ((RxByte & 0b11100000) == 0b11000000)
                            q--;
                    }
                    else
                    {
                        // Sysex start. Sysex is 3-byte payload.
                        if (RxByte == 0xF0)
                            q++;
                        // 0xF1 Time Code, 0xF3 Song Select, are 1-byte payload.
                        else if (RxByte != 0xF2)
                            q--;
                        // Here is only 0xF2 passed, it is 2-byte payload.

                        buf[0] = q;  // Table 4-1: 2, 4, or 3
                    }
                }
                else // if(RxByte < 0x80)
                {
                    if (q == 4) // SysEx: 6 -> 7 -> 5 -> 6 -> 7 -> 5 -> 6 -> 7...
                    {
                        if (p == 5)
                        {
                            buf[1] = RxByte;
                            p++;
                        }
                        else if (p == 6)
                        {
                            buf[2] = RxByte;
                            p++;
                        }
                        else // 7
                        {
                            buf[3] = RxByte;
                            p = 5;
                            usb_tx_buffer (buf, 4);
                            n++;        // Q'ty of 4-byte chunks
                            if (n >= 7) // Like similar to Axiom 25 for simpler debug.
                            {
                                n = 0;
                                goto utxrdy5;
                            }
                        }
                    }
                    else if (q == 3)
                    {
                        if (p == 6)
                        {
                            buf[2] = RxByte;
                            p++;
                        }
                        else // 7
                        {
                            p = 6;
                            goto utxrdy4;
                        }
                    }
                    else if (q == 2)
                        goto utxrdy3;

                    // Here q = 0, or no multibyte status byte received so far: LED remains lit.
                    goto notyet;
                }

                goto notyet;

utxrdy1:
                q = 0;
                buf[0] = 0x05;   // 1-byte
utxrdy2:
                buf[1] = RxByte;
                buf[2] = 0;
                buf[3] = 0;
                goto utxrdy4a;
utxrdy3:
                buf[2] = RxByte;
                buf[3] = 0;
                goto utxrdy4a;
utxrdy4:
                buf[3] = RxByte;
utxrdy4a:
                usb_tx_buffer (buf, 4);
utxrdy5:
                usb_tx_push();
stop:
                cbi(PORTC, 7);
notyet:
            }

            // Phase 2. Check if USB packet received, get bytes and emit on MIDI OUT.
            while(usb_rx_available())
            {
                sbi(PORTC, 7); // TODO Make it to fire only once, as at Echo.

                // https://www.usb.org/sites/default/files/midi10.pdf
                // USB packets are not just copy of MIDI cable bytes, they are
                // at least have special one byte header, as per Section 4.
                // Header byte defines quantity of MIDI bytes from constant
                // length packet, and this definition is not direct, but with
                // lookup table.
                uint8_t byte0 = usb_rx_char();

                // Should be fast enough, but need to use table and IJMP later.
                if (byte0 < 2) // Reserved, throw away.
                {
                    usb_rx_char();
                    usb_rx_char();
                    usb_rx_char();
                    // cbi(PORTC, 7);
                }
                else if ((byte0 == 5) || (byte0 == 15))
                {
                    ringbuf[uwptr++] = usb_rx_char();
//                        uwptr &= TX_MASK; // We have exactly one byte size pointers.
                    usb_rx_char();
                    usb_rx_char();
                }
                else if ((byte0 == 2) || (byte0 == 6) || (byte0 == 12) || (byte0 == 13))
                {
                    ringbuf[uwptr++] = usb_rx_char();
                    ringbuf[uwptr++] = usb_rx_char();
                    usb_rx_char();
                }
                else
                {
                    ringbuf[uwptr++] = usb_rx_char();
                    ringbuf[uwptr++] = usb_rx_char();
                    ringbuf[uwptr++] = usb_rx_char();
                }
            }

            if( (UCSR1A & (1<<UDRE1)) && uwptr!=irptr ) {
                UDR1 = ringbuf[irptr++];
                // irptr &= TX_MASK; // We have exactly one byte size pointers.
            }

            // Wait for actual end of serial transmission, to correctly turn LED off.
            // Note that if no one byte was transmitted, like thrown away packet,
            // this flag not fires, and LED continues to lit until valid transmission.
            // It can help to detect errors.
            // Or, turn on 'cbi...' above at throw away section.
            if(UCSR1A & (1<<TXC1)) {
                sbi(UCSR1A, TXC1); // Clear this flag, to fire only once.
                cbi(PORTC, 7);
            }
        }
    }
}
