
// Experimental. Please use with caution.

/*
Based on:
https://www.w3tutorials.net/blog/how-to-set-alternate-setting-for-the-usb-device-using-libusb/
 and
https://code-examples.net/en/q/4c12b32/reverse-engineering-logitech-quickcam-correctly-using-libusb-set-interface-alt-setting

License:  GNU GPL v2 or later.
*/

// gcc -lusb-1.0 altset.c

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <unistd.h>

#define VID 0x16c0
#define PID 0x27de

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;
    struct libusb_config_descriptor *config = NULL;
    int err;

    int interface_number = 1;

    // Default is 1 for MIDI 2.0, so we try to switch 1 -> 0.
    int altset = 0;

    err = ! libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
    if (err) {
        fprintf(stderr, "Libusb does not supports detaching of the default USB driver with libusb_detach_kernel_driver().\n");
        goto done0;
    }

    err = libusb_init(&ctx);
    if (err < 0) {
        fprintf(stderr, "Failed to initialize libusb:\n");
        goto done1;
    }

    libusb_set_debug(ctx, 3); // Or 4 is even better.

    dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!dev_handle) {
        fprintf(stderr, "Failed to open USB device. Check VID/PID or permissions.\n");
        goto done2;
    }

    int speed = libusb_get_device_speed(libusb_get_device(dev_handle));
    printf("Wire speed is: %d\n", speed);

    err = libusb_get_config_descriptor(libusb_get_device(dev_handle), 0, &config);
    if (err < 0) {
        fprintf(stderr, "Failed to get config descriptor:\n");
        goto done3;
    }

#if 0
    printf("Found %d interfaces in configuration %d:\n", config->bNumInterfaces, config->bConfigurationValue);
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];
        printf("Interface %d has %d alternate settings:\n", i, iface->num_altsetting);

        for (int a = 0; a < iface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            printf("  Alternate %d: %d endpoints\n", a, alt->bNumEndpoints);

            for (int e = 0; e < alt->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                printf("    Endpoint 0x%02X: %s, %s\n",
                    ep->bEndpointAddress,
                    (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK ? "Bulk" :
                    (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_INTERRUPT ? "Interrupt" : "Control",
                    (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ? "IN" : "OUT");
            }
        }
    }
#endif

    if (libusb_kernel_driver_active(dev_handle, interface_number)) {
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
        // Handle error, e.g., check for -6 (LIBUSB_ERROR_BUSY) which means a kernel driver is still attached!
        goto done4;
    }

    printf("Interface %d claimed successfully.\n", interface_number);

    err = libusb_set_interface_alt_setting(dev_handle, interface_number, altset);

    if (err < 0) {
        fprintf(stderr, "Error setting alternate setting:\n");
        goto done4;
    }

    printf("Successfully set alternate setting %d for interface %d.\n", altset, interface_number);

    err = libusb_release_interface(dev_handle, interface_number);

    if (err < 0) {
        fprintf(stderr, "Error releasing interface: %s\n");
        goto done4;
    }

    printf("Successfully released interface %d.\n", interface_number);


//    libusb_reset_device(dev_handle);

    // sleep(1); // For better usmbon debug.
    //
    // err = libusb_attach_kernel_driver(dev_handle, interface_number);
    // if (err < 0) {
    //     fprintf(stderr, "Error attaching kernel driver:\n");
    //     goto done4;
    // }
    //
    // printf("Kernel driver attached.\n");


done4:
    libusb_free_config_descriptor(config);
done3:
    libusb_close(dev_handle);
done2:
    libusb_exit(NULL);
done1:
    if (err)
        fprintf(stderr, "%s\n", libusb_error_name(err));
done0:
    return err;
}
