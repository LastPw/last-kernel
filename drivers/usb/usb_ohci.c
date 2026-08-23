#include "usb.h"
#include "pci.h"
#include "paging.h"
#include "io.h"
#include "keyboard.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "xhci.h"

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB 0x03u
#define PCI_PROGIF_OHCI 0x10u
#define PCI_PROGIF_EHCI 0x20u
#define PCI_PROGIF_XHCI 0x30u

#define OHCI_REG_REVISION 0x00u
#define OHCI_REG_CONTROL 0x04u
#define OHCI_REG_COMMAND_STATUS 0x08u
#define OHCI_REG_INTERRUPT_STATUS 0x0Cu
#define OHCI_REG_INTERRUPT_DISABLE 0x14u
#define OHCI_REG_HCCA 0x18u
#define OHCI_REG_PERIOD_CURRENT_ED 0x1Cu
#define OHCI_REG_CONTROL_HEAD_ED 0x20u
#define OHCI_REG_CONTROL_CURRENT_ED 0x24u
#define OHCI_REG_BULK_HEAD_ED 0x28u
#define OHCI_REG_BULK_CURRENT_ED 0x2Cu
#define OHCI_REG_RH_DESCRIPTOR_A 0x48u
#define OHCI_REG_RH_PORT_STATUS 0x54u

#define OHCI_CMD_HCR 0x00000001u
#define OHCI_CMD_CLF 0x00000002u
#define OHCI_CTRL_PLE 0x00000004u
#define OHCI_CTRL_CLE 0x00000010u
#define OHCI_CTRL_BLE 0x00000020u
#define OHCI_CTRL_OPERATIONAL 0x00000080u
#define OHCI_ED_LOWSPEED 0x00002000u
#define OHCI_TD_CC_NOT_ACCESSED 0xF0000000u
#define OHCI_TD_CC_MASK 0xF0000000u
#define OHCI_TD_DI_7 0x00E00000u
#define OHCI_TD_DATA0 0x02000000u
#define OHCI_TD_DATA1 0x03000000u
#define OHCI_TD_IN 0x00100000u
#define OHCI_TD_OUT 0x00080000u
#define OHCI_TD_SETUP 0x00000000u
#define OHCI_RHPS_CCS 0x00000001u
#define OHCI_RHPS_PES 0x00000002u
#define OHCI_RHPS_PRS 0x00000010u
#define OHCI_RHPS_LSDA 0x00000200u

#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIGURATION 2u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u
#define USB_CLASS_HID 3u
#define USB_HID_BOOT_SUBCLASS 1u
#define USB_HID_KEYBOARD_PROTOCOL 1u
#define USB_HID_MOUSE_PROTOCOL 2u
#define USB_REQ_GET_DESCRIPTOR 6u
#define USB_REQ_SET_ADDRESS 5u
#define USB_REQ_SET_CONFIGURATION 9u
#define USB_HID_SET_IDLE 0x0Au
#define USB_HID_SET_PROTOCOL 0x0Bu
#define USB_MASS_RESET 0xFFu
#define USB_MASS_GET_MAX_LUN 0xFEu
#define USB_CLASS_MASS_STORAGE 8u
#define USB_MASS_SUBCLASS_SCSI 6u
#define USB_MASS_PROTOCOL_BULK_ONLY 0x50u

typedef struct __attribute__((packed, aligned(16))) {
    u32 info;
    u32 tail;
    u32 head;
    u32 next;
} ohci_ed_t;

typedef struct __attribute__((packed, aligned(16))) {
    u32 info;
    u32 cbp;
    u32 next;
    u32 be;
} ohci_td_t;

typedef struct __attribute__((packed)) {
    u8 request_type;
    u8 request;
    u16 value;
    u16 index;
    u16 length;
} usb_setup_t;

typedef struct {
    u8 active;
    u8 port;
    u8 address;
    u8 endpoint;
    u8 low_speed;
    u16 packet_size;
    u8 toggle;
    u8 previous_report[8];
} usb_hid_state_t;

typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 transfer_length;
    u8 flags;
    u8 lun;
    u8 command_length;
    u8 command[16];
} usb_mass_cbw_t;

typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 residue;
    u8 status;
} usb_mass_csw_t;

typedef struct {
    u8 active;
    u8 address;
    u8 low_speed;
    u8 in_endpoint;
    u8 out_endpoint;
    u16 in_packet;
    u16 out_packet;
    u16 control_packet;
    u32 tag;
} usb_mass_state_t;

static usb_status_t g_usb;
static usb_hid_state_t g_hid;
static u8 g_hcca[256] __attribute__((aligned(256)));
static ohci_ed_t g_control_ed __attribute__((aligned(16)));
static ohci_td_t g_control_td[4] __attribute__((aligned(16)));
static ohci_ed_t g_hid_ed __attribute__((aligned(16)));
static ohci_td_t g_hid_td[2] __attribute__((aligned(16)));
static ohci_ed_t g_bulk_ed __attribute__((aligned(16)));
static ohci_td_t g_bulk_td[2] __attribute__((aligned(16)));
static usb_setup_t g_setup __attribute__((aligned(16)));
static u8 g_transfer_data[256] __attribute__((aligned(16)));
static u8 g_hid_report[8] __attribute__((aligned(16)));
static u8 g_mass_data[512] __attribute__((aligned(16)));
static usb_mass_cbw_t g_mass_cbw __attribute__((aligned(16)));
static usb_mass_csw_t g_mass_csw __attribute__((aligned(16)));
static usb_mass_state_t g_mass;
static u32 g_next_enumeration_tick;

static u32 ohci_read(u32 offset) {
    return mmio_read32(g_usb.mmio_base + offset);
}

static void ohci_write(u32 offset, u32 value) {
    mmio_write32(g_usb.mmio_base + offset, value);
}

static int ohci_wait_reset(void) {
    for (u32 spin = 0u; spin < 1000000u; spin++) {
        if ((ohci_read(OHCI_REG_COMMAND_STATUS) & OHCI_CMD_HCR) == 0u) return 0;
    }
    return -1;
}

/* USB reset timing is specified in milliseconds; spin counts are not stable in VMs. */
static void usb_delay_ms(u32 milliseconds) {
    u32 frequency = timer_frequency();
    u32 needed_ticks = (milliseconds * frequency + 999u) / 1000u;
    u32 start = timer_ticks();
    u32 guard = 30000000u;

    if (needed_ticks == 0u) needed_ticks = 1u;
    while ((u32)(timer_ticks() - start) < needed_ticks && guard--) { }
}

static int ohci_td_result(const ohci_td_t* td) {
    u32 cc = (td->info & OHCI_TD_CC_MASK) >> 28u;
    if (cc == 0x0Fu) return 1; /* Controller has not reached this descriptor yet. */
    return cc == 0u ? 0 : -1;
}

static void usb_make_setup(u8 type, u8 request, u16 value, u16 index, u16 length) {
    g_setup.request_type = type;
    g_setup.request = request;
    g_setup.value = value;
    g_setup.index = index;
    g_setup.length = length;
}

static int ohci_control_transfer(u8 address, u8 low_speed, u16 max_packet,
                                 const usb_setup_t* setup, u8* data, u16 length, u8 data_in) {
    ohci_td_t* setup_td = &g_control_td[0];
    ohci_td_t* data_td = &g_control_td[1];
    ohci_td_t* status_td = &g_control_td[2];
    ohci_td_t* tail_td = &g_control_td[3];
    u32 status_pid = data_in ? OHCI_TD_OUT : OHCI_TD_IN;
    u32 next_after_setup = length ? (u32)data_td : (u32)status_td;

    if (!setup || max_packet == 0u || max_packet > 64u || length > sizeof(g_transfer_data)) return -1;
    memset(g_control_td, 0, sizeof(g_control_td));
    memset(&g_control_ed, 0, sizeof(g_control_ed));
    memcpy(&g_setup, setup, sizeof(g_setup));

    setup_td->info = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_7 | OHCI_TD_DATA0 | OHCI_TD_SETUP;
    setup_td->cbp = (u32)&g_setup;
    setup_td->next = next_after_setup;
    setup_td->be = (u32)&g_setup + sizeof(g_setup) - 1u;
    if (length) {
        data_td->info = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_7 | OHCI_TD_DATA1 |
                        (data_in ? OHCI_TD_IN : OHCI_TD_OUT);
        data_td->cbp = (u32)data;
        data_td->next = (u32)status_td;
        data_td->be = (u32)data + length - 1u;
    }
    status_td->info = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_7 | OHCI_TD_DATA1 | status_pid;
    status_td->next = (u32)tail_td;

    g_control_ed.info = (u32)address | ((u32)max_packet << 16u) |
                        (low_speed ? OHCI_ED_LOWSPEED : 0u);
    g_control_ed.tail = (u32)tail_td;
    g_control_ed.head = (u32)setup_td;
    ohci_write(OHCI_REG_CONTROL_HEAD_ED, (u32)&g_control_ed);
    ohci_write(OHCI_REG_CONTROL_CURRENT_ED, 0u);
    ohci_write(OHCI_REG_CONTROL, OHCI_CTRL_OPERATIONAL | OHCI_CTRL_CLE);
    ohci_write(OHCI_REG_COMMAND_STATUS, OHCI_CMD_CLF);

    for (u32 spin = 0u; spin < 2000000u; spin++) {
        int result = ohci_td_result(status_td);
        if (result <= 0) {
            ohci_write(OHCI_REG_CONTROL_HEAD_ED, 0u);
            return result;
        }
    }
    ohci_write(OHCI_REG_CONTROL_HEAD_ED, 0u);
    return -1;
}

static int ohci_bulk_transfer(u8 endpoint, u8 low_speed, u16 max_packet,
                              u8 in, u8* data, u16 length) {
    ohci_td_t* td = &g_bulk_td[0];
    ohci_td_t* tail = &g_bulk_td[1];
    if (!g_usb.controller_ready || !data || !length || max_packet == 0u || max_packet > 64u) return -1;
    memset(g_bulk_td, 0, sizeof(g_bulk_td));
    memset(&g_bulk_ed, 0, sizeof(g_bulk_ed));
    td->info = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_7 | (in ? OHCI_TD_IN : OHCI_TD_OUT);
    td->cbp = (u32)data;
    td->next = (u32)tail;
    td->be = (u32)data + length - 1u;
    g_bulk_ed.info = (u32)g_mass.address | ((u32)(endpoint & 0x0Fu) << 7u) |
                     ((u32)max_packet << 16u) | 0x2u | (low_speed ? OHCI_ED_LOWSPEED : 0u);
    g_bulk_ed.tail = (u32)tail;
    g_bulk_ed.head = (u32)td;
    ohci_write(OHCI_REG_BULK_HEAD_ED, (u32)&g_bulk_ed);
    ohci_write(OHCI_REG_BULK_CURRENT_ED, 0u);
    ohci_write(OHCI_REG_CONTROL, OHCI_CTRL_OPERATIONAL | OHCI_CTRL_BLE);
    for (u32 spin = 0u; spin < 2000000u; spin++) {
        int result = ohci_td_result(td);
        if (result <= 0) { ohci_write(OHCI_REG_BULK_HEAD_ED, 0u); return result; }
    }
    ohci_write(OHCI_REG_BULK_HEAD_ED, 0u);
    return -1;
}

static void mass_put_be32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24u); p[1] = (u8)(v >> 16u); p[2] = (u8)(v >> 8u); p[3] = (u8)v;
}

static u32 mass_get_be32(const u8* p) {
    return ((u32)p[0] << 24u) | ((u32)p[1] << 16u) | ((u32)p[2] << 8u) | p[3];
}

static int usb_mass_command(const u8* command, u8 command_length, u8* data, u32 length, u8 in) {
    if (!g_mass.active || !command || command_length == 0u || command_length > 16u || length > sizeof(g_mass_data)) return -1;
    memset(&g_mass_cbw, 0, sizeof(g_mass_cbw));
    g_mass_cbw.signature = 0x43425355u;
    g_mass_cbw.tag = ++g_mass.tag;
    g_mass_cbw.transfer_length = length;
    g_mass_cbw.flags = in ? 0x80u : 0u;
    g_mass_cbw.command_length = command_length;
    memcpy(g_mass_cbw.command, command, command_length);
    if (ohci_bulk_transfer(g_mass.out_endpoint, g_mass.low_speed, g_mass.out_packet, 0u, (u8*)&g_mass_cbw, sizeof(g_mass_cbw)) != 0) return -1;
    if (length && ohci_bulk_transfer(in ? g_mass.in_endpoint : g_mass.out_endpoint, g_mass.low_speed,
                                      in ? g_mass.in_packet : g_mass.out_packet, in, data, (u16)length) != 0) return -1;
    memset(&g_mass_csw, 0, sizeof(g_mass_csw));
    if (ohci_bulk_transfer(g_mass.in_endpoint, g_mass.low_speed, g_mass.in_packet, 1u,
                           (u8*)&g_mass_csw, sizeof(g_mass_csw)) != 0) return -1;
    return g_mass_csw.signature == 0x53425355u && g_mass_csw.tag == g_mass_cbw.tag && g_mass_csw.status == 0u ? 0 : -1;
}

static int usb_mass_probe(void) {
    u8 command[16];
    u8 inquiry[36];
    u8 capacity[8];
    memset(command, 0, sizeof(command)); memset(inquiry, 0, sizeof(inquiry)); memset(capacity, 0, sizeof(capacity));
    command[0] = 0x12u; command[4] = sizeof(inquiry);
    if (usb_mass_command(command, 6u, inquiry, sizeof(inquiry), 1u) != 0) return -1;
    memset(command, 0, sizeof(command)); command[0] = 0x25u;
    if (usb_mass_command(command, 10u, capacity, sizeof(capacity), 1u) != 0) return -1;
    g_usb.mass_block_count = mass_get_be32(capacity) + 1u;
    g_usb.mass_block_size = mass_get_be32(capacity + 4u);
    if (g_usb.mass_block_size != 512u || g_usb.mass_block_count == 0u) return -1;
    g_usb.mass_storage_ready = 1u;
    return 0;
}

static int usb_root_port_enable(u8 port) {
    u32 reg = OHCI_REG_RH_PORT_STATUS + ((u32)port * 4u);
    u32 state = ohci_read(reg);
    if ((state & OHCI_RHPS_CCS) == 0u) return -1;
    ohci_write(reg, OHCI_RHPS_PRS);
    usb_delay_ms(20u);
    for (u32 spin = 0u; spin < 1000000u; spin++) {
        state = ohci_read(reg);
        if ((state & OHCI_RHPS_PRS) == 0u) break;
    }
    for (u32 spin = 0u; spin < 1000000u; spin++) {
        if (ohci_read(reg) & OHCI_RHPS_PES) return 0;
    }
    return -1;
}

static char hid_usage_to_char(u8 usage, u8 shift) {
    static const char digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";
    if (usage >= 0x04u && usage <= 0x1Du) {
        char c = (char)('a' + (usage - 0x04u));
        return shift ? (char)(c - ('a' - 'A')) : c;
    }
    if (usage >= 0x1Eu && usage <= 0x27u) return shift ? shifted_digits[usage - 0x1Eu] : digits[usage - 0x1Eu];
    if (usage == 0x28u) return '\n';
    if (usage == 0x2Au) return '\b';
    if (usage == 0x2Bu) return '\t';
    if (usage == 0x2Cu) return ' ';
    if (usage == 0x2Du) return shift ? '_' : '-';
    if (usage == 0x2Eu) return shift ? '+' : '=';
    if (usage == 0x2Fu) return shift ? '{' : '[';
    if (usage == 0x30u) return shift ? '}' : ']';
    if (usage == 0x31u) return shift ? '|' : '\\';
    if (usage == 0x33u) return shift ? ':' : ';';
    if (usage == 0x34u) return shift ? '"' : '\'';
    if (usage == 0x36u) return shift ? '<' : ',';
    if (usage == 0x37u) return shift ? '>' : '.';
    if (usage == 0x38u) return shift ? '?' : '/';
    return 0;
}

static int hid_report_has_key(const u8* report, u8 key) {
    for (u32 i = 2u; i < 8u; i++) if (report[i] == key) return 1;
    return 0;
}

static void usb_hid_keyboard_report(void) {
    u8 shift = (g_hid_report[0] & 0x22u) ? 1u : 0u;
    for (u32 i = 2u; i < 8u; i++) {
        u8 usage = g_hid_report[i];
        char c;
        if (usage == 0u || hid_report_has_key(g_hid.previous_report, usage)) continue;
        c = hid_usage_to_char(usage, shift);
        if (c) keyboard_inject_char(c);
    }
    memcpy(g_hid.previous_report, g_hid_report, sizeof(g_hid.previous_report));
}

static void usb_hid_submit(void) {
    ohci_td_t* td = &g_hid_td[0];
    ohci_td_t* tail = &g_hid_td[1];
    if (!g_usb.hid_keyboard || g_hid.active || g_hid.packet_size == 0u) return;
    memset(g_hid_td, 0, sizeof(g_hid_td));
    memset(&g_hid_ed, 0, sizeof(g_hid_ed));
    memset(g_hid_report, 0, sizeof(g_hid_report));
    td->info = OHCI_TD_CC_NOT_ACCESSED | OHCI_TD_DI_7 |
               (g_hid.toggle ? OHCI_TD_DATA1 : OHCI_TD_DATA0) | OHCI_TD_IN;
    td->cbp = (u32)g_hid_report;
    td->next = (u32)tail;
    td->be = (u32)g_hid_report + g_hid.packet_size - 1u;
    g_hid_ed.info = (u32)g_hid.address | ((u32)g_hid.endpoint << 7u) |
                    ((u32)g_hid.packet_size << 16u) |
                    (g_hid.low_speed ? OHCI_ED_LOWSPEED : 0u);
    g_hid_ed.tail = (u32)tail;
    g_hid_ed.head = (u32)td;
    for (u32 slot = 0u; slot < 32u; slot++) ((u32*)g_hcca)[slot] = (u32)&g_hid_ed;
    ohci_write(OHCI_REG_PERIOD_CURRENT_ED, 0u);
    ohci_write(OHCI_REG_CONTROL, OHCI_CTRL_OPERATIONAL | OHCI_CTRL_CLE | OHCI_CTRL_PLE);
    g_hid.active = 1u;
}

static void usb_hid_poll_transfer(void) {
    int result;
    if (!g_hid.active) return;
    result = ohci_td_result(&g_hid_td[0]);
    if (result > 0) return;
    g_hid.active = 0u;
    if (result == 0) {
        g_hid.toggle ^= 1u;
        g_usb.hid_reports++;
        usb_hid_keyboard_report();
    }
    usb_hid_submit();
}

static int usb_enumerate_hid_port(u8 port) {
    u8 device[18];
    u16 max_packet;
    u16 total;
    u8 config_value;
    u8 interface_number = 0u;
    u8 protocol = 0u;
    u8 endpoint = 0u;
    u16 packet_size = 0u;
    u8 low_speed;
    u8 device_address = (u8)(port + 1u);
    u8 mass_found = 0u;
    u8 mass_in = 0u, mass_out = 0u;
    u16 mass_in_packet = 0u, mass_out_packet = 0u;

    if (usb_root_port_enable(port) != 0) return -10;
    low_speed = (ohci_read(OHCI_REG_RH_PORT_STATUS + ((u32)port * 4u)) & OHCI_RHPS_LSDA) ? 1u : 0u;
    memset(device, 0, sizeof(device));
    usb_make_setup(0x80u, USB_REQ_GET_DESCRIPTOR, (u16)(USB_DESC_DEVICE << 8u), 0u, sizeof(device));
    if (ohci_control_transfer(0u, low_speed, 8u, &g_setup, device, sizeof(device), 1u) != 0) return -11;
    max_packet = device[7];
    if (max_packet == 0u || max_packet > 64u) return -12;

    usb_make_setup(0x00u, USB_REQ_SET_ADDRESS, device_address, 0u, 0u);
    if (ohci_control_transfer(0u, low_speed, max_packet, &g_setup, 0, 0u, 0u) != 0) return -13;
    usb_delay_ms(5u);

    memset(g_transfer_data, 0, sizeof(g_transfer_data));
    usb_make_setup(0x80u, USB_REQ_GET_DESCRIPTOR, (u16)(USB_DESC_CONFIGURATION << 8u), 0u, 9u);
    if (ohci_control_transfer(device_address, low_speed, max_packet, &g_setup, g_transfer_data, 9u, 1u) != 0) return -14;
    total = (u16)g_transfer_data[2] | ((u16)g_transfer_data[3] << 8u);
    if (total < 9u || total > sizeof(g_transfer_data)) return -15;
    config_value = g_transfer_data[5];
    usb_make_setup(0x80u, USB_REQ_GET_DESCRIPTOR, (u16)(USB_DESC_CONFIGURATION << 8u), 0u, total);
    if (ohci_control_transfer(device_address, low_speed, max_packet, &g_setup, g_transfer_data, total, 1u) != 0) return -16;

    for (u32 at = 0u; at + 2u <= total;) {
        u8 length = g_transfer_data[at];
        u8 type = g_transfer_data[at + 1u];
        if (length < 2u || at + length > total) break;
        if (type == USB_DESC_INTERFACE && length >= 9u) {
            if (g_transfer_data[at + 5u] == USB_CLASS_HID &&
                g_transfer_data[at + 6u] == USB_HID_BOOT_SUBCLASS &&
                (g_transfer_data[at + 7u] == USB_HID_KEYBOARD_PROTOCOL ||
                 g_transfer_data[at + 7u] == USB_HID_MOUSE_PROTOCOL)) {
                interface_number = g_transfer_data[at + 2u];
                protocol = g_transfer_data[at + 7u];
            } else if (g_transfer_data[at + 5u] == USB_CLASS_MASS_STORAGE &&
                       g_transfer_data[at + 6u] == USB_MASS_SUBCLASS_SCSI &&
                       g_transfer_data[at + 7u] == USB_MASS_PROTOCOL_BULK_ONLY) {
                mass_found = 1u;
                protocol = 0u;
            } else {
                protocol = 0u;
            }
        } else if (type == USB_DESC_ENDPOINT && length >= 7u && protocol != 0u &&
                   (g_transfer_data[at + 2u] & 0x80u) && (g_transfer_data[at + 3u] & 0x03u) == 0x03u) {
            endpoint = (u8)(g_transfer_data[at + 2u] & 0x0Fu);
            packet_size = (u16)g_transfer_data[at + 4u] | ((u16)g_transfer_data[at + 5u] << 8u);
            break;
        } else if (type == USB_DESC_ENDPOINT && length >= 7u && mass_found &&
                   (g_transfer_data[at + 3u] & 0x03u) == 0x02u) {
            u8 ep = (u8)(g_transfer_data[at + 2u] & 0x0Fu);
            u16 packet = (u16)g_transfer_data[at + 4u] | ((u16)g_transfer_data[at + 5u] << 8u);
            if (g_transfer_data[at + 2u] & 0x80u) { mass_in = ep; mass_in_packet = packet; }
            else { mass_out = ep; mass_out_packet = packet; }
        }
        at += length;
    }
    if (mass_found && mass_in && mass_out && mass_in_packet && mass_out_packet) {
        usb_make_setup(0x00u, USB_REQ_SET_CONFIGURATION, config_value, 0u, 0u);
        if (ohci_control_transfer(device_address, low_speed, max_packet, &g_setup, 0, 0u, 0u) != 0) return -18;
        usb_delay_ms(5u);
        memset(&g_mass, 0, sizeof(g_mass));
        g_mass.active = 1u; g_mass.address = device_address; g_mass.low_speed = low_speed;
        g_mass.in_endpoint = mass_in; g_mass.out_endpoint = mass_out;
        g_mass.in_packet = mass_in_packet; g_mass.out_packet = mass_out_packet;
        g_mass.control_packet = max_packet;
        g_usb.mass_storage_found = 1u; g_usb.mass_address = device_address;
        g_usb.mass_in_endpoint = mass_in; g_usb.mass_out_endpoint = mass_out;
        g_usb.mass_in_packet = mass_in_packet; g_usb.mass_out_packet = mass_out_packet;
        if (usb_mass_probe() != 0) { g_mass.active = 0u; g_usb.mass_storage_found = 0u; return -22; }
        return -23;
    }
    if (protocol == 0u || endpoint == 0u || packet_size == 0u || packet_size > sizeof(g_hid_report)) return -17;
    /* This driver binds one boot keyboard; a mouse must not consume that slot. */
    if (protocol != USB_HID_KEYBOARD_PROTOCOL) return -21;

    usb_make_setup(0x00u, USB_REQ_SET_CONFIGURATION, config_value, 0u, 0u);
    if (ohci_control_transfer(device_address, low_speed, max_packet, &g_setup, 0, 0u, 0u) != 0) return -18;
    usb_delay_ms(5u);
    usb_make_setup(0x21u, USB_HID_SET_PROTOCOL, 0u, interface_number, 0u);
    /* VBox's virtual HID can already be in boot protocol and STALL this optional request. */
    (void)ohci_control_transfer(1u, low_speed, max_packet, &g_setup, 0, 0u, 0u);
    usb_make_setup(0x21u, USB_HID_SET_IDLE, 0u, interface_number, 0u);
    (void)ohci_control_transfer(1u, low_speed, max_packet, &g_setup, 0, 0u, 0u);

    memset(&g_hid, 0, sizeof(g_hid));
    g_hid.port = port;
    g_hid.address = device_address;
    g_hid.endpoint = endpoint;
    g_hid.low_speed = low_speed;
    g_hid.packet_size = packet_size;
    g_usb.hid_address = device_address;
    g_usb.hid_endpoint = endpoint;
    g_usb.hid_packet_size = packet_size;
    g_usb.hid_keyboard = 1u;
    usb_hid_submit();
    return 0;
}

static void usb_refresh_ports(void) {
    g_usb.connected_mask = 0u;
    g_usb.low_speed_mask = 0u;
    for (u32 port = 0u; port < g_usb.port_count; port++) {
        u32 state = ohci_read(OHCI_REG_RH_PORT_STATUS + (port * 4u));
        if (state & OHCI_RHPS_CCS) g_usb.connected_mask |= (1u << port);
        if (state & OHCI_RHPS_LSDA) g_usb.low_speed_mask |= (1u << port);
    }
    g_usb.enumerated_ports &= g_usb.connected_mask;
}

static void usb_try_enumerate_hid(void) {
    u32 now = timer_ticks();
    if (!g_usb.connected_mask || (i32)(now - g_next_enumeration_tick) < 0) return;
    g_next_enumeration_tick = now + (timer_frequency() / 4u) + 1u;
    for (u8 port = 0u; port < g_usb.port_count; port++) {
        int result;
        if ((g_usb.connected_mask & (1u << port)) == 0u) continue;
        if (g_usb.enumerated_ports & (1u << port)) continue;
        result = usb_enumerate_hid_port(port);
        if (result == 0) {
            g_usb.enumerated_ports |= (1u << port);
            serial_write("[LastOS] USB HID ready on port ");
            serial_write_dec(port + 1u);
            serial_putc('\n');
            continue;
        }
        if (result == -21 || result == -23) {
            if (result == -23) {
                g_usb.enumerated_ports |= (1u << port);
                serial_write("[LastOS] USB Mass Storage ready sectors="); serial_write_dec(g_usb.mass_block_count); serial_putc('\n');
            }
            continue;
        }
        g_usb.enumeration_failures++;
        g_usb.last_error = (u32)(-result);
        serial_write("[LastOS] USB HID enumeration failed on port ");
        serial_write_dec(port + 1u);
        serial_write(" stage ");
        serial_write_dec(g_usb.last_error);
        serial_putc('\n');
    }
}

void usb_poll(void) {
    /* Poll xHCI if available (handles HID keyboard + mouse) */
    if (xhci_available()) {
        xhci_poll();
    }
    /* Poll OHCI if available (legacy USB 1.x HID + mass storage) */
    if (!g_usb.controller_ready) return;
    usb_refresh_ports();
    usb_try_enumerate_hid();
    usb_hid_poll_transfer();
}

int usb_init(void) {
    pci_device_t dev;
    u32 descriptor_a;

    memset(&g_usb, 0, sizeof(g_usb));
    memset(&g_hid, 0, sizeof(g_hid));
    g_next_enumeration_tick = 0u;
    if (pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_EHCI, 0) == 0) g_usb.ehci_found = 1u;
    if (pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_XHCI, 0) == 0) g_usb.xhci_found = 1u;
    if (pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_OHCI, &dev) != 0) {
        /* No OHCI controller found. Try xHCI instead. */
        if (xhci_init() == 0) {
            serial_writeln("[LastOS] USB xHCI initialized");
            /* xHCI handles its own HID polling internally; do NOT set
               g_usb.controller_ready = 1u because that would make the
               OHCI poll path attempt to read un-mapped OHCI registers. */
            g_usb.xhci_found = 1u;
            g_usb.controller_kind = 3u; /* xHCI-only */
            return 0;
        }
        serial_writeln("[LastOS] USB xHCI unavailable, falling back to USB subsystem");
        return -1;
    }
    if ((dev.bar0 & 1u) != 0u) return -1;

    g_usb.ohci_found = 1u;
    g_usb.controller_kind = 1u;
    g_usb.vendor = dev.vendor;
    g_usb.mmio_base = dev.bar0 & 0xFFFFF000u;
    if (!g_usb.mmio_base || pci_enable_bus_master(&dev) != 0) return -1;
    if (!paging_is_mapped(g_usb.mmio_base) &&
        paging_map_range(g_usb.mmio_base, g_usb.mmio_base, 4096u,
                         PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD) != 0) return -1;
    if (ohci_read(OHCI_REG_REVISION) == 0xFFFFFFFFu) return -1;

    memset(g_hcca, 0, sizeof(g_hcca));
    ohci_write(OHCI_REG_INTERRUPT_DISABLE, 0xFFFFFFFFu);
    ohci_write(OHCI_REG_COMMAND_STATUS, OHCI_CMD_HCR);
    if (ohci_wait_reset() != 0) {
        g_usb.reset_failures++;
        return -1;
    }
    ohci_write(OHCI_REG_HCCA, (u32)g_hcca);
    ohci_write(OHCI_REG_INTERRUPT_STATUS, 0xFFFFFFFFu);
    ohci_write(OHCI_REG_CONTROL, OHCI_CTRL_OPERATIONAL | OHCI_CTRL_CLE);
    descriptor_a = ohci_read(OHCI_REG_RH_DESCRIPTOR_A);
    g_usb.port_count = (u8)(descriptor_a & 0xFFu);
    if (g_usb.port_count > USB_MAX_ROOT_PORTS) g_usb.port_count = USB_MAX_ROOT_PORTS;
    g_usb.controller_ready = 1u;
    usb_refresh_ports();
    serial_write("[LastOS] USB OHCI ports=");
    serial_write_dec(g_usb.port_count);
    serial_write(" connected=0x");
    for (i32 shift = 28; shift >= 0; shift -= 4) {
        static const char hex[] = "0123456789ABCDEF";
        serial_putc(hex[(g_usb.connected_mask >> (u32)shift) & 0x0Fu]);
    }
    serial_putc('\n');
    return 0;
}

int usb_ready(void) {
    return g_usb.controller_ready ? 1 : 0;
}

int usb_mass_storage_ready(void) { return g_usb.mass_storage_ready ? 1 : 0; }
u32 usb_mass_storage_sector_count(void) { return g_usb.mass_block_count; }

int usb_mass_storage_read_sector(u32 lba, u8* out512) {
    u8 command[16];
    if (!usb_mass_storage_ready() || !out512 || lba >= g_usb.mass_block_count) return -1;
    memset(command, 0, sizeof(command)); command[0] = 0x28u; mass_put_be32(command + 2u, lba); command[7] = 0u; command[8] = 1u;
    return usb_mass_command(command, 10u, out512, 512u, 1u);
}

int usb_mass_storage_write_sector(u32 lba, const u8* in512) {
    u8 command[16];
    if (!usb_mass_storage_ready() || !in512 || lba >= g_usb.mass_block_count) return -1;
    memcpy(g_mass_data, in512, 512u);
    memset(command, 0, sizeof(command)); command[0] = 0x2Au; mass_put_be32(command + 2u, lba); command[7] = 0u; command[8] = 1u;
    return usb_mass_command(command, 10u, g_mass_data, 512u, 0u);
}

void usb_status(usb_status_t* out) {
    if (!out) return;
    *out = g_usb;
}
