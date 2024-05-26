#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "libusb.h"

#define VID (uint16_t) 0x320f
#define PID (uint16_t) 0x50c2
#define COMMAND_ENDPOINT (unsigned char) 0x05
#define INTERRUPT_ENDPOINT (unsigned char) 0x82

static libusb_device_handle *devh = NULL;
static volatile sig_atomic_t do_exit = 0;
static volatile sig_atomic_t pending_transaction = 0;

// static void request_exit(sig_atomic_t code) {
//     do_exit = code;
// }

static void LIBUSB_CALL end_transaction(struct libusb_transfer *transfer) {
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr, "Transaction failed: %d | %s\n", transfer->status, libusb_strerror(transfer->status));
    }
    // printf("Transaction succeeded.\n");
    
    pending_transaction = 0;
}

static void LIBUSB_CALL receive(struct libusb_transfer *transfer) {
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr, "Submission failed: %d | %s\n", transfer->status, libusb_strerror(transfer->status));
    }
    // printf("Submission succeeded.\n");

    struct libusb_transfer *new_transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    new_transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(new_transfer, devh, INTERRUPT_ENDPOINT, buf, 64, end_transaction, NULL, 1000);
    libusb_submit_transfer(new_transfer);
}

static int submit_footer() {
    pending_transaction = 1;
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    buf[0] = 0x04; buf[1] = 0x02; buf[2] = 0x00; buf[3] = 0x02;
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(transfer, devh, COMMAND_ENDPOINT, buf, 64, receive, NULL, 1000);
    return libusb_submit_transfer(transfer);
}

// static void LIBUSB_CALL post_body(struct libusb_transfer *transfer) {
//     if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
//         fprintf(stderr, "Body transfer failed: %d | %s\n", transfer->status, libusb_strerror(transfer->status));
//         request_exit(2);
//     }
//     printf("Body successfuly transmitted. Now submitting footer.\n");

//     submit_footer();
// }

static int submit_brightness(unsigned char brightness) {
    pending_transaction = 1;
    if (brightness > 4) {
        fprintf(stderr, "Illegal brightness level: %d\n", brightness);
        // request_exit(2);
    }

    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    
    // 04 1d 02 06 1d 00 00 00 00 06 00 02 00 00 72 ff
    // 80 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 
    buf[0] = 0x04; buf[2] = 0x02; buf[3] = 0x06; buf[4] = 0x1d; buf[9] = 0x06; buf[11] = 0x02;
    buf[14] = 0x72; buf[15] = 0xff; buf[16] = 0x80; buf[26] = 0x01;
    buf[1] = 0x1d + brightness; 
    buf[10] = 0x00 + brightness;
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(transfer, devh, COMMAND_ENDPOINT, buf, 64, receive, NULL, 1000);
    return libusb_submit_transfer(transfer);
}

// static void LIBUSB_CALL post_header(struct libusb_transfer *transfer) {
//     if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
//         fprintf(stderr, "Header transfer failed: %d | %s\n", transfer->status, libusb_strerror(transfer->status));
//         request_exit(2);
//     }
//     printf("Header successfuly transmitted. Now submitting brightness.\n");

//     submit_brightness();
// }

static int is_ajazz(libusb_device *dev) {
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    return (desc.idVendor == VID && desc.idProduct == PID) ? 1 : 0;
}

static int submit_header() {
    pending_transaction = 1;
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    buf[0] = 0x04; buf[1] = 0x01; buf[2] = 0x00; buf[3] = 0x01;
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(transfer, devh, COMMAND_ENDPOINT, buf, 64, receive, NULL, 1000);
    return libusb_submit_transfer(transfer);
}

static void set_brightness(unsigned char brightness) {
    submit_header();
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - header\n");
			// request_exit(2);
        }
    }
    submit_brightness(brightness);
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - body\n");
			// request_exit(2);
        }
    }
    submit_footer();
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - footer\n");
			// request_exit(2);
        }
    }
    printf("Brightness set to level: %d\n", brightness);
}

static void handle_kb(libusb_device *found, libusb_device **list) {
    int err = libusb_open(found, &devh);
    if (err) {
        fprintf(stderr, "Failed ot open device: %d | %s\n", err, libusb_strerror(err));
        libusb_free_device_list(list, 1);
        libusb_exit(NULL);
        exit(1);
    }
    libusb_free_device_list(list, 1);

    // struct libusb_config_descriptor *config;
    // err = libusb_get_active_config_descriptor(found, &config);
    // if (err) {
    //     fprintf(stderr, "Failed to get config descriptor: %d | %s\n", err, libusb_strerror(err));
    //     libusb_close(devh);
    //     libusb_free_device_list(list, 1);
    //     libusb_exit(NULL);
    //     exit(1);
    // }

    err = libusb_detach_kernel_driver(devh, 1);
    if (err) {
        fprintf(stderr, "Failed to detach kernel driver: %d | %s\n", err, libusb_strerror(err));
        // libusb_free_config_descriptor(config);
        libusb_close(devh);
        // libusb_free_device_list(list, 1);
        libusb_exit(NULL);
        exit(1);
    }

    err = libusb_claim_interface(devh, 1);
    if (err) {
        fprintf(stderr, "Failed to claim interface: %d | %s\n", err, libusb_strerror(err));
        // libusb_free_config_descriptor(config);
        libusb_close(devh);
        // libusb_free_device_list(list, 1);
        libusb_exit(NULL);
        exit(1);
    }

    // void change_settings()
    // struct libusb_interface_descriptor iface = config->interface[1].altsetting[0];
    // printf("%02x:%02x\n", iface.endpoint[0].bEndpointAddress, iface.endpoint[1].bEndpointAddress);

    // set_brightness(0);
    // sleep(1);
    // set_brightness(1);
    // sleep(1);
    // set_brightness(2);
    // sleep(1);
    // set_brightness(3);
    // sleep(1);
    // set_brightness(4);
    // do_exit = 1;

    // while (!do_exit) {
	// 	err = libusb_handle_events(NULL);
	// 	if (err < 0) {
    //         printf("eee\n");
	// 		request_exit(2);
    //     }
    // }

    err = libusb_release_interface(devh, 1);
    if (err) {
        fprintf(stderr, "Failed to release interface: %d | %s\n", err, libusb_strerror(err));
        // libusb_free_config_descriptor(config);
        libusb_close(devh);
        // libusb_free_device_list(list, 1);
        libusb_exit(NULL);
        exit(1);
    }

    err = libusb_attach_kernel_driver(devh, 1);
    if (err) {
        fprintf(stderr, "Failed to attach kernel driver: %d | %s\n", err, libusb_strerror(err));
        // libusb_free_config_descriptor(config);
        libusb_close(devh);
        // libusb_free_device_list(list, 1);
        libusb_exit(NULL);
        exit(1);
    }

    // libusb_free_config_descriptor(config);

    libusb_close(devh);
}

int main() {
    // printf("Initializing...");
    int r = libusb_init_context(NULL, NULL, 0);
    if (r < 0) {
        fprintf(stderr, "Failed to initialize libusb: %d | %s\n", r, libusb_strerror(r));
        exit(1);
    }
    // printf(" success!\n");

    // printf("This application ");
    // printf(libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) ? "has" : "does NOT have");
    // printf(" hotplot support!\n");

    // get kb
    libusb_device **list;
    libusb_device *found = NULL;
    ssize_t cnt = libusb_get_device_list(NULL, &list);
    ssize_t i = 0;
    // int err = 0;
    if (cnt < 0) {
        fprintf(stderr, "Failed to get device list: %d | %s\n", r, libusb_strerror(r));
        libusb_exit(NULL);
        exit(1);
    }

    for (i = 0; i < cnt; i++) {
        libusb_device *device = list[i];
        if (is_ajazz(device)) {
            found = device;
            break;
        }
    }

    if (found) {
        handle_kb(found, list);
    }

    // libusb_free_device_list(list, 1);

    libusb_exit(NULL);
    return r;
}