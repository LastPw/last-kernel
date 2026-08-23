#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

#include "pci.h"
#include "types.h"

typedef struct {
    pci_device_t pci;
    u16 io_base;
    u32 common_cfg;
    u32 notify_cfg;
    u32 notify_multiplier;
    u32 device_cfg;
    u8 modern;
} virtio_pci_transport_t;

int virtio_pci_probe(virtio_pci_transport_t* transport);
/* Probe a specific legacy/modern device pair (for example net or block). */
int virtio_pci_probe_device(virtio_pci_transport_t* transport,
                            u16 legacy_device, u16 modern_device);
void virtio_pci_reset(virtio_pci_transport_t* transport);
void virtio_pci_set_status(virtio_pci_transport_t* transport, u8 status);
u8 virtio_pci_get_status(const virtio_pci_transport_t* transport);

#endif
