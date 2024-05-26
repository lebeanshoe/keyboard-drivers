#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "libusb.h"

#define SUCCESS_CODE 0
#define FAIL_CODE 1

#define VID (uint16_t) 0x320f
#define PID (uint16_t) 0x50c2
#define IFACE_NUM 1
#define COMMAND_ENDPOINT (unsigned char) 0x05
#define INTERRUPT_ENDPOINT (unsigned char) 0x82

static libusb_device_handle *devh = NULL;
static volatile sig_atomic_t do_exit = 0;
static volatile sig_atomic_t pending_transaction = 0;

// SETUP FUNCTIONS
void teardown(int stat) {
    if (devh) {
        int err = libusb_release_interface(devh, IFACE_NUM);
        if (err) {
            fprintf(stderr, "Failed to release interface: %d | %s\n", err, libusb_strerror(err));
            stat = FAIL_CODE;
        }
        printf("iface released\n");

        if (!libusb_kernel_driver_active(devh, 1)) {
            err = libusb_attach_kernel_driver(devh, IFACE_NUM);
            if (err) {
                fprintf(stderr, "Failed to attach kernel driver: %d | %s\n", err, libusb_strerror(err));
                stat = FAIL_CODE;
            }
            printf("kernel driver reattached\n");
        }
    }

    libusb_close(devh);
    
    libusb_exit(NULL);
    if (stat == FAIL_CODE) exit(stat);
    return;
}

void get_device() {
    libusb_device **list;
    ssize_t cnt = libusb_get_device_list(NULL, &list);
    ssize_t i = 0;
    if (cnt < 0) {
        fprintf(stderr, "Failed to get device list: %ld | %s\n", cnt, libusb_strerror(cnt));
        libusb_exit(NULL);
        exit(1);
    }

    for (i = 0; i < cnt; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(device, &desc);
        if (desc.idVendor == VID && desc.idProduct == PID) {
            libusb_open(device, &devh);
            if (devh) break;
        }
    }

    libusb_free_device_list(list, 1);
    if (!devh) {
        teardown(FAIL_CODE);
    }
}


void init_config() {
    int r = libusb_init_context(NULL, NULL, 0);
    if (r < 0) {
        fprintf(stderr, "Failed to initialize libusb: %d | %s\n", r, libusb_strerror(r));
        exit(FAIL_CODE);
    }

    get_device();
    if (!devh) {
        fprintf(stderr, "Failed to open device, aborting\n");
        teardown(FAIL_CODE);
    }

    int err;

    if (libusb_kernel_driver_active(devh, IFACE_NUM)) {
        err = libusb_detach_kernel_driver(devh, IFACE_NUM);
        if (err) {
            fprintf(stderr, "Failed to detach kernel driver: %d | %s\n", err, libusb_strerror(err));
            teardown(FAIL_CODE);
        }
    }

    err = libusb_claim_interface(devh, IFACE_NUM);
    if (err) {
        fprintf(stderr, "Failed to claim interface: %d | %s\n", err, libusb_strerror(err));
        teardown(FAIL_CODE);
    }
}

// CONFIG FUNCTIONS
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

static int submit_header() {
    pending_transaction = 1;
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    buf[0] = 0x04; buf[1] = 0x01; buf[2] = 0x00; buf[3] = 0x01;
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(transfer, devh, COMMAND_ENDPOINT, buf, 64, receive, NULL, 1000);
    return libusb_submit_transfer(transfer);
}

static int submit_brightness(unsigned char brightness) {
    pending_transaction = 1;
    if (brightness > 4) {
        fprintf(stderr, "Illegal brightness level: %d\n", brightness);
    }

    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = calloc(64, sizeof(unsigned char));
    
    // 04 1d 02 06 1d 00 00 00 00 06 00 02 00 00 72 ff
    // 80 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 
    buf[0] = 0x04; buf[2] = 0x02; buf[3] = 0x06; buf[4] = 0x1d; buf[9] = 0x06; buf[11] = 0x02;
    buf[14] = 0x72; buf[15] = 0xff; buf[16] = 0x80; buf[26] = 0x01;
    buf[1] = 0x1d + brightness; 
    buf[10] = brightness;
    transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK | LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    libusb_fill_interrupt_transfer(transfer, devh, COMMAND_ENDPOINT, buf, 64, receive, NULL, 1000);
    return libusb_submit_transfer(transfer);
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

static void set_brightness(unsigned char brightness) {
    submit_header();
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - header\n");
        }
    }
    submit_brightness(brightness);
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - body\n");
        }
    }
    submit_footer();
    while (pending_transaction) {
		int err = libusb_handle_events(NULL);
		if (err < 0) {
            printf("problem while setting brightness - footer\n");
        }
    }
    printf("Brightness set to level: %d\n", brightness);
}

int main() {
    init_config();

    set_brightness(0);
    sleep(1);
    set_brightness(1);
    sleep(1);
    set_brightness(2);
    sleep(1);
    set_brightness(3);
    sleep(1);
    set_brightness(4);

    teardown(SUCCESS_CODE);
    return 0;
}