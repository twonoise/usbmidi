
/*
Based on:
https://www.w3tutorials.net/blog/how-to-set-alternate-setting-for-the-usb-device-using-libusb/
 and
https://code-examples.net/en/q/4c12b32/reverse-engineering-logitech-quickcam-correctly-using-libusb-set-interface-alt-setting

License:  GNU GPL v2 or later.
*/

// gcc -lusb-1.0 setspeed.c

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, const char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s serialnumber baudrate.\n", argv[0]);
        return 1;
    }

    uint8_t sn[33];
    strncpy(sn, argv[1], sizeof(sn));

    uint32_t bps = strtoul(argv[2], 0, 10);

    // U32 baud, U8 stopbits, U8 parity, U8 bits
    uint8_t cdc_line_coding[7] = {0,0,0,0,0,0,8};
    *(uint32_t *)cdc_line_coding = bps;

    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;
    struct libusb_config_descriptor *config = NULL;

    int interface_number = 1;

    libusb_set_debug(ctx, 3);

    int err;

    err = libusb_init(&ctx);
    if (err < 0) {
        fprintf(stderr, "Failed to initialize libusb:\n");
        goto done1;
    }

    libusb_device **devs;

    int n = libusb_get_device_list(ctx, &devs);

    if (n < 0) {
        fprintf(stderr, "Failed to get USB device list:\n");
        goto done2;
    }

    struct libusb_device_descriptor desc;
    int i;

    for (i = 0; i < n; i++) {
        err = libusb_get_device_descriptor(devs[i], &desc);
        if (err < 0) {
            fprintf(stderr, "Device handle not received:\n");
            goto done2;
        }

        uint8_t data[33];
        libusb_open(devs[i], &dev_handle);
        if (dev_handle != nullptr) {
            if (libusb_get_string_descriptor_ascii(dev_handle, desc.iSerialNumber, data, 31) >= 0) {
                data[32] = '\0';
                if (strcmp(data, sn) == 0)
                    break;
            }
            libusb_close(dev_handle);
        }
    }

    if (i == n) {
        fprintf(stderr, "Serial Number \"%s\" not found, sorry.\n", sn);
        goto done2;
    }

    int speed = libusb_get_device_speed(libusb_get_device(dev_handle));
    printf("Found good one: %02X:%02X, wire speed is: %d.\n",
           desc.idVendor, desc.idProduct, speed);

    err = libusb_get_config_descriptor(libusb_get_device(dev_handle), 0, &config);
    if (err < 0) {
        fprintf(stderr, "Failed to get config descriptor:\n");
        goto done3;
    }

    if (libusb_kernel_driver_active(dev_handle, interface_number)) {
        err = ! libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
        if (err) {
            fprintf(stderr, "Libusb does not supports detaching of the default USB driver with libusb_detach_kernel_driver().\n");
            goto done4;
        }
        printf("Kernel driver active, attempting to detach...\n");
        err = libusb_detach_kernel_driver(dev_handle, interface_number);
        if (err < 0) {
            fprintf(stderr, "Error detaching kernel driver:\n");
            fprintf(stderr, "You might need to use a udev rule or similar persistent method if this fails, but try to proceed anyway, as some systems allow claim even if detach fails.\n");
            goto done4;
        } else
            printf("Kernel driver detached.\n");
    }

    err = libusb_claim_interface(dev_handle, interface_number);

    if (err < 0) {
        fprintf(stderr, "Error claiming interface %d:\n", interface_number);
        goto done4;
    }

    printf("Interface %d claimed successfully.\n", interface_number);

    // bRequest:       0x20 CDC_SET_LINE_CODING
    // bmRequestType:  0x21 for set
    err = libusb_control_transfer(dev_handle, 0x21, 0x20, 0, 1, cdc_line_coding, 7, 1000);

    if (err < 0) {
        fprintf(stderr, "Error sending control transfer:\n");
        goto done4;
    }

    printf("Successfully send control transfer.\n");

    // bRequest:       0x21 CDC_GET_LINE_CODING
    // bmRequestType:  0xA1 for get
    err = libusb_control_transfer(dev_handle, 0xA1, 0x21, 0, 1, cdc_line_coding, 7, 1000);

    if (err < 0) {
        fprintf(stderr, "Error reading control transfer:\n");
        goto done4;
    }

    uint32_t bps_readback = *(uint32_t *)cdc_line_coding;
    if (bps_readback == bps) {
        printf("Successfully read and compared control transfer value.\n");
    } else {
        fprintf(stderr, " Read value %d does not match to send one.\n", bps_readback);
    }

    err = libusb_release_interface(dev_handle, interface_number);

    if (err < 0) {
        fprintf(stderr, "Error releasing interface:\n");
        goto done4;
    }

    printf("Successfully released interface %d.\n", interface_number);


done4:
    libusb_free_config_descriptor(config);

done3:
    libusb_close(dev_handle);

done2:
    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);

done1:
    if (err)
        fprintf(stderr, "%s\n", libusb_error_name(err));

done0:
    return err;
}
