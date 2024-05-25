#include <iostream>
#include <chrono>
#include <thread>
#include <string>

extern "C" {
    #include "libusb.h"
}

#define VID (uint16_t) 0x320f
#define PID (uint16_t) 0x50c2
#define COMMAND_ENDPOINT (unsigned char) 0x05
#define INTERRUPT_ENDPOINT (unsigned char) 0x82

bool is_ajazz(libusb_device *dev) {
    libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    if (desc.idVendor == VID && desc.idProduct == PID) return true;

    return false;
}

void LIBUSB_CALL transfer_cb(struct libusb_transfer *transfer) {
    std::cout << "here" << std::endl;
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        std::cout << "Transfer completed successfully." << std::endl;
    } else {
        std::cerr << "Transfer failed: " << libusb_strerror(transfer->status) << std::endl;
    }
    libusb_free_transfer(transfer);
}

int main() {
    std::cout << "Starting" << std::endl;

    int r = libusb_init_context(NULL, NULL, 0);
    if (r < 0) {
        std::cerr << "Failed to initialise libusb: " << r << " | " << libusb_strerror(r) << std::endl;
        exit(1);
    }

    // get kb
    libusb_device **list;
    libusb_device *found = NULL;
    ssize_t cnt = libusb_get_device_list(NULL, &list);
    ssize_t i = 0;
    int err = 0;
    if (cnt < 0) {
        std::cerr << "Failed to get device list: " << cnt << " | " << libusb_strerror(cnt) << std::endl;
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
        libusb_device_handle *handle;

        err = libusb_open(found, &handle);
        if (err) {
            std::cerr << "Failed to open device: " << err << " - " << libusb_strerror(err) << std::endl;
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        libusb_config_descriptor *config;
        err = libusb_get_active_config_descriptor(found, &config);
        if (err) {
            std::cerr << "Failed to get config descriptor: " << err << " | " << libusb_strerror(err) << std::endl;
            libusb_close(handle);
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        err = libusb_detach_kernel_driver(handle, 1);
        if (err) {
            std::cerr << "Failed to detach kernel driver: " << err << " | " << libusb_strerror(err) << std::endl;
            libusb_free_config_descriptor(config);
            libusb_close(handle);
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        err = libusb_claim_interface(handle, 1);
        if (err) {
            std::cerr << "Failed to claim interface: " << err << " | " << libusb_strerror(err) << std::endl;
            libusb_free_config_descriptor(config);
            libusb_close(handle);
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        libusb_interface_descriptor iface = config->interface[1].altsetting[0];
        std::cerr << std::hex << +iface.endpoint[0].bEndpointAddress << ":" << +iface.endpoint[1].bEndpointAddress << std::endl;

        int capability = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
        std::string msg = capability == 0 ? "No hotplug" : "Has hotplug";
        std::cout <<  msg << std::endl;

        // max brightness
        libusb_transfer *transfer = libusb_alloc_transfer(64);
        unsigned char *buf = new unsigned char[64]();
        buf[0] = 0x04; buf[1] = 0x01; buf[2] = 0x00; buf[3] = 0x01;
        libusb_fill_interrupt_transfer(transfer, handle, COMMAND_ENDPOINT, buf, 64, transfer_cb, NULL, 1000);
        // err = libusb_submit_transfer(transfer);
        // if (err) {
        //     std::cerr << "Failed to submit transfer: " << err << " | " << libusb_strerror(err) << std::endl;
        //     libusb_free_config_descriptor(config);
        //     libusb_close(handle);
        //     libusb_free_device_list(list, 1);
        //     libusb_exit(NULL);
        //     exit(1);
        // }
        libusb_free_transfer(transfer); // needs to be in callback
        delete[] buf; // needs to be in callback

        err = libusb_release_interface(handle, 1);
        if (err) {
            std::cerr << "Failed to release interface: " << err << " | " << libusb_strerror(err) << std::endl;
            libusb_free_config_descriptor(config);
            libusb_close(handle);
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        err = libusb_attach_kernel_driver(handle, 1);
        if (err) {
            std::cerr << "Failed to attach kernel driver: " << err << " | " << libusb_strerror(err) << std::endl;
            libusb_free_config_descriptor(config);
            libusb_close(handle);
            libusb_free_device_list(list, 1);
            libusb_exit(NULL);
            exit(1);
        }

        libusb_free_config_descriptor(config);

        libusb_close(handle);
    }

    libusb_free_device_list(list, 1);

    libusb_exit(NULL);
    return r >= 0? r : -r;
}