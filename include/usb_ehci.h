#ifndef USB_EHCI_H
#define USB_EHCI_H

#include "types.h"

void usb_ehci_init(void);
int  usb_ehci_available(void);
u8   usb_ehci_port_count(void);

#endif
