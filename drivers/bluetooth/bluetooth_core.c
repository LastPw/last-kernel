#include "bluetooth.h"
#include "string.h"
#include "spinlock.h"
#include "timer.h"
#include "screen.h"

/* ==============================================================================
 * Bluetooth Core Subsystem Implementation
 * ============================================================================== */

static bluetooth_subsystem_status_t s_bt;
static spinlock_t s_bt_lock;

/* Pre-allocated ACL Fragment Reassembly Buffer */
#define BT_SAR_BUF_SIZE 4096

/* Local Controller Mock/Hardware Output Hook */
typedef int (*bt_hci_tx_fn)(const u8* buf, u16 len);
static bt_hci_tx_fn s_active_driver_tx = (void*)0;

void bluetooth_set_driver_tx(bt_hci_tx_fn tx_func) {
    s_active_driver_tx = tx_func;
}

static void bt_send_hci_cmd(u16 opcode, const u8* params, u8 param_len) {
    u8 pkt[260];
    pkt[0] = HCI_COMMAND_PKT;
    hci_cmd_hdr_t* hdr = (hci_cmd_hdr_t*)&pkt[1];
    hdr->opcode = opcode;
    hdr->param_len = param_len;
    if (params && param_len > 0) {
        for (u8 i = 0; i < param_len; i++) {
            pkt[1 + sizeof(hci_cmd_hdr_t) + i] = params[i];
        }
    }
    if (s_active_driver_tx) {
        s_active_driver_tx(pkt, (u16)(1 + sizeof(hci_cmd_hdr_t) + param_len));
    }
}

/* ==============================================================================
 * Initialization & Device State Table
 * ============================================================================== */

void bluetooth_init(void) {
    spinlock_init(&s_bt_lock);
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);

    s_bt.initialized = 1;
    s_bt.scanning = 0;
    s_bt.local_addr.b[0] = 0xAA;
    s_bt.local_addr.b[1] = 0xBB;
    s_bt.local_addr.b[2] = 0xCC;
    s_bt.local_addr.b[3] = 0x11;
    s_bt.local_addr.b[4] = 0x22;
    s_bt.local_addr.b[5] = 0x33;

    str_ncpy(s_bt.local_name, "LastOS-Bluetooth-Station", sizeof(s_bt.local_name));
    s_bt.hci_version = 0x0C;     /* Bluetooth 5.3 Core Spec */
    s_bt.manufacturer = 0x0002;    /* Intel / Standard */
    s_bt.total_devices_discovered = 0;
    s_bt.active_connections_count = 0;
    s_bt.gatt_attrs_count = 0;

    for (u32 i = 0; i < BT_MAX_DEVICES; i++) {
        s_bt.devices[i].paired = 0;
        s_bt.devices[i].name[0] = '\0';
        s_bt.devices[i].last_seen_tick = 0;
    }

    for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
        s_bt.connections[i].handle = 0;
        s_bt.connections[i].state = BT_DEV_DISCONNECTED;
        s_bt.connections[i].acl_mtu = 1024;
        s_bt.connections[i].rx_packets = 0;
        s_bt.connections[i].tx_packets = 0;
        s_bt.connections[i].rx_bytes = 0;
        s_bt.connections[i].tx_bytes = 0;
            }

    /* Initialize Default Standard GATT Services */
    bluetooth_register_gatt_service(GATT_UUID_GENERIC_ACCESS);
    bluetooth_register_gatt_char(0x2A00, 0x02, (const u8*)"LastOS-PC", 9); /* Device Name */
    bluetooth_register_gatt_char(0x2A01, 0x02, (const u8*)"\x80\x00", 2);   /* Appearance: Computer */

    bluetooth_register_gatt_service(GATT_UUID_BATTERY_SERVICE);
    bluetooth_register_gatt_char(0x2A19, 0x02, (const u8*)"\x64", 1);       /* 100% Battery */

    bluetooth_register_gatt_service(GATT_UUID_DEVICE_INFO);
    bluetooth_register_gatt_char(0x2A29, 0x02, (const u8*)"Axior Corp", 10);/* Manufacturer */
    bluetooth_register_gatt_char(0x2A24, 0x02, (const u8*)"LastKernel-v2", 13);/* Model */

    spinlock_unlock_irqrestore(&s_bt_lock, flags);

    /* Send Initial HCI Reset Sequence */
    bt_send_hci_cmd(HCI_CMD_RESET, (void*)0, 0);
    bt_send_hci_cmd(HCI_CMD_READ_BD_ADDR, (void*)0, 0);
    bt_send_hci_cmd(HCI_CMD_READ_LOCAL_VERSION, (void*)0, 0);
}

/* ==============================================================================
 * Scanning & Device Discovery
 * ============================================================================== */

int bluetooth_start_scan(u8 le_scan) {
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    s_bt.scanning = 1;
    spinlock_unlock_irqrestore(&s_bt_lock, flags);

    if (le_scan) {
        /* Set LE Scan Parameters (Active Scanning, 100ms interval, 50ms window) */
        u8 params[7] = { 0x01, 0xA0, 0x00, 0x50, 0x00, 0x00, 0x00 };
        bt_send_hci_cmd(HCI_CMD_LE_SET_SCAN_PARAM, params, sizeof(params));

        /* Enable LE Scanning */
        u8 enable[2] = { 0x01, 0x00 }; /* Enable, filter duplicates */
        bt_send_hci_cmd(HCI_CMD_LE_SET_SCAN_ENABLE, enable, sizeof(enable));
    } else {
        /* Classic BR/EDR General Inquiry (GIAC, 10.24s duration, unlimited responses) */
        u8 inq[5] = { 0x33, 0x8B, 0x9E, 0x08, 0x00 };
        bt_send_hci_cmd(HCI_CMD_INQUIRY, inq, sizeof(inq));
    }
    return 0;
}

int bluetooth_stop_scan(void) {
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    s_bt.scanning = 0;
    spinlock_unlock_irqrestore(&s_bt_lock, flags);

    u8 disable[2] = { 0x00, 0x00 };
    bt_send_hci_cmd(HCI_CMD_LE_SET_SCAN_ENABLE, disable, sizeof(disable));
    bt_send_hci_cmd(HCI_CMD_INQUIRY_CANCEL, (void*)0, 0);
    return 0;
}

static void record_discovered_device(const bd_addr_t* addr, const char* name, u32 cod, i8 rssi, bt_device_type_t dev_type) {
    if (!addr) return;
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);

    /* Search for existing device */
    for (u32 i = 0; i < s_bt.total_devices_discovered && i < BT_MAX_DEVICES; i++) {
        if (memcmp(s_bt.devices[i].addr.b, addr->b, 6) == 0) {
            if (name && name[0]) str_ncpy(s_bt.devices[i].name, name, sizeof(s_bt.devices[i].name));
            s_bt.devices[i].rssi = rssi;
            s_bt.devices[i].last_seen_tick = timer_ticks();
            spinlock_unlock_irqrestore(&s_bt_lock, flags);
            return;
        }
    }

    /* Add new device record */
    if (s_bt.total_devices_discovered < BT_MAX_DEVICES) {
        u32 idx = s_bt.total_devices_discovered++;
        memcpy(s_bt.devices[idx].addr.b, addr->b, 6);
        if (name && name[0]) {
            str_ncpy(s_bt.devices[idx].name, name, sizeof(s_bt.devices[idx].name));
        } else {
            str_ncpy(s_bt.devices[idx].name, "Unknown Bluetooth Device", sizeof(s_bt.devices[idx].name));
        }
        s_bt.devices[idx].class_of_device = cod;
        s_bt.devices[idx].rssi = rssi;
        s_bt.devices[idx].dev_type = dev_type;
        s_bt.devices[idx].paired = 0;
        s_bt.devices[idx].last_seen_tick = timer_ticks();
    }

    spinlock_unlock_irqrestore(&s_bt_lock, flags);
}

/* ==============================================================================
 * Connection Management
 * ============================================================================== */

int bluetooth_connect(const bd_addr_t* addr) {
    if (!addr) return -1;
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);

    /* Find a free connection slot */
    int slot = -1;
    for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (s_bt.connections[i].state == BT_DEV_DISCONNECTED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        spinlock_unlock_irqrestore(&s_bt_lock, flags);
        return -2;
    }

    s_bt.connections[slot].handle = (u16)(0x0040 + slot);
    memcpy(s_bt.connections[slot].remote_addr.b, addr->b, 6);
    s_bt.connections[slot].state = BT_DEV_CONNECTING;
    s_bt.connections[slot].local_cid = (u16)(L2CAP_CID_DYN_START + slot);
    s_bt.active_connections_count++;

    spinlock_unlock_irqrestore(&s_bt_lock, flags);

    /* Send HCI Create Connection */
    u8 params[13];
    memcpy(&params[0], addr->b, 6);
    params[6] = 0x18; params[7] = 0xCC; /* Packet type (DM1, DH1, DM3, DH3, DM5, DH5) */
    params[8] = 0x02;                   /* Page scan repetition mode R2 */
    params[9] = 0x00;                   /* Reserved */
    params[10] = 0x00; params[11] = 0x00;/* Clock offset */
    params[12] = 0x01;                  /* Allow role switch */
    bt_send_hci_cmd(HCI_CMD_CREATE_CONN, params, sizeof(params));

    return (int)s_bt.connections[slot].handle;
}

int bluetooth_disconnect(u16 conn_handle) {
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (s_bt.connections[i].handle == conn_handle && s_bt.connections[i].state != BT_DEV_DISCONNECTED) {
            s_bt.connections[i].state = BT_DEV_DISCONNECTED;
            if (s_bt.active_connections_count > 0) s_bt.active_connections_count--;
            spinlock_unlock_irqrestore(&s_bt_lock, flags);

            u8 params[3];
            params[0] = (u8)(conn_handle & 0xFF);
            params[1] = (u8)((conn_handle >> 8) & 0x0F);
            params[2] = HCI_ERR_REMOTE_USER_TERMINATED;
            bt_send_hci_cmd(HCI_CMD_DISCONNECT, params, sizeof(params));
            return 0;
        }
    }
    spinlock_unlock_irqrestore(&s_bt_lock, flags);
    return -1;
}

/* ==============================================================================
 * L2CAP Packet Transmitter & SAR Assembly
 * ============================================================================== */

int bluetooth_send_acl(u16 conn_handle, u16 cid, const u8* data, u16 len) {
    if (!data || len == 0 || len > (BT_SAR_BUF_SIZE - sizeof(l2cap_hdr_t))) return -1;

    u8 pkt[1024];
    pkt[0] = HCI_ACL_DATA_PKT;

    hci_acl_hdr_t* acl = (hci_acl_hdr_t*)&pkt[1];
    u16 total_l2cap_len = (u16)(sizeof(l2cap_hdr_t) + len);
    acl->handle_flags = (conn_handle & 0x0FFF) | (0x02 << 12); /* PB=10 (first non-auto-flushable) */
    acl->data_len = total_l2cap_len;

    l2cap_hdr_t* l2cap = (l2cap_hdr_t*)&pkt[1 + sizeof(hci_acl_hdr_t)];
    l2cap->length = len;
    l2cap->cid = cid;

    u8* payload = &pkt[1 + sizeof(hci_acl_hdr_t) + sizeof(l2cap_hdr_t)];
    for (u16 i = 0; i < len; i++) {
        payload[i] = data[i];
    }

    u32 total_wire_len = 1 + sizeof(hci_acl_hdr_t) + sizeof(l2cap_hdr_t) + len;
    if (s_active_driver_tx) {
        s_active_driver_tx(pkt, (u16)total_wire_len);
    }

    /* Update connection metrics */
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (s_bt.connections[i].handle == conn_handle) {
            s_bt.connections[i].tx_packets++;
            s_bt.connections[i].tx_bytes += total_wire_len;
            break;
        }
    }
    spinlock_unlock_irqrestore(&s_bt_lock, flags);
    return 0;
}

/* ==============================================================================
 * A2DP SBC Audio Stream Packetizer
 * ============================================================================== */

int bluetooth_send_audio_sbc(u16 conn_handle, const u8* pcm_audio, u16 samples) {
    if (!pcm_audio || samples == 0) return -1;

    /* Construct A2DP RTP SBC Audio Payload */
    u8 audio_frame[512];
    a2dp_sbc_media_packet_t* rtp = (a2dp_sbc_media_packet_t*)audio_frame;

    /* RTP Header (RFC 3550: V=2, P=0, X=0, CC=0, M=1, PT=96 Dynamic) */
    rtp->rtp_header[0] = 0x80;
    rtp->rtp_header[1] = 0x60 | 0x80; /* PT=96, Marker=1 */
    rtp->rtp_header[2] = 0x01; rtp->rtp_header[3] = 0x23; /* Sequence number */
    rtp->rtp_header[4] = 0x00; rtp->rtp_header[5] = 0x00; rtp->rtp_header[6] = 0x10; rtp->rtp_header[7] = 0x00; /* Timestamp */
    rtp->rtp_header[8] = 0x12; rtp->rtp_header[9] = 0x34; rtp->rtp_header[10] = 0x56; rtp->rtp_header[11] = 0x78; /* SSRC */
    rtp->sbc_frames_count = 1;

    /* SBC Encoded Sub-band Frame Header (Syncword 0x9C, 44.1kHz, 16 blocks, Joint Stereo, Loudness, 8 subbands, Bitpool 35) */
    u8* sbc = &audio_frame[sizeof(a2dp_sbc_media_packet_t)];
    sbc[0] = 0x9C; /* SBC Syncword */
    sbc[1] = 0x2B; /* 44.1kHz / 16 blocks / Joint Stereo / Loudness */
    sbc[2] = 35;   /* Bitpool = 35 (High Quality Audio) */
    sbc[3] = 0x00; /* CRC checksum */

    /* Copy sample payload */
    u16 payload_bytes = (samples > 128) ? 128 : samples;
    for (u16 i = 0; i < payload_bytes; i++) {
        sbc[4 + i] = pcm_audio[i];
    }

    u16 frame_total = (u16)(sizeof(a2dp_sbc_media_packet_t) + 4 + payload_bytes);
    return bluetooth_send_acl(conn_handle, L2CAP_PSM_AVDTP, audio_frame, frame_total);
}

/* ==============================================================================
 * GATT Server Registry
 * ============================================================================== */

int bluetooth_register_gatt_service(u16 service_uuid) {
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    if (s_bt.gatt_attrs_count >= BT_MAX_GATT_ATTRS) {
        spinlock_unlock_irqrestore(&s_bt_lock, flags);
        return -1;
    }
    u32 idx = s_bt.gatt_attrs_count++;
    s_bt.gatt_table[idx].handle = (u16)(idx + 1);
    s_bt.gatt_table[idx].uuid16 = 0x2800; /* Primary Service Declaration */
    s_bt.gatt_table[idx].permissions = 0x02; /* Read-Only */
    s_bt.gatt_table[idx].value_len = 2;
    s_bt.gatt_table[idx].value[0] = (u8)(service_uuid & 0xFF);
    s_bt.gatt_table[idx].value[1] = (u8)((service_uuid >> 8) & 0xFF);
    spinlock_unlock_irqrestore(&s_bt_lock, flags);
    return (int)s_bt.gatt_table[idx].handle;
}

int bluetooth_register_gatt_char(u16 char_uuid, u8 permissions, const u8* initial_val, u16 val_len) {
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    if (s_bt.gatt_attrs_count >= BT_MAX_GATT_ATTRS) {
        spinlock_unlock_irqrestore(&s_bt_lock, flags);
        return -1;
    }
    u32 idx = s_bt.gatt_attrs_count++;
    s_bt.gatt_table[idx].handle = (u16)(idx + 1);
    s_bt.gatt_table[idx].uuid16 = char_uuid;
    s_bt.gatt_table[idx].permissions = permissions;
    s_bt.gatt_table[idx].value_len = (val_len > 64) ? 64 : val_len;
    if (initial_val && s_bt.gatt_table[idx].value_len > 0) {
        for (u16 i = 0; i < s_bt.gatt_table[idx].value_len; i++) {
            s_bt.gatt_table[idx].value[i] = initial_val[i];
        }
    }
    spinlock_unlock_irqrestore(&s_bt_lock, flags);
    return (int)s_bt.gatt_table[idx].handle;
}

/* ==============================================================================
 * Incoming HCI Packet Processing (Events & ACL Data)
 * ============================================================================== */

void bluetooth_on_hci_packet(const u8* packet, u16 len) {
    if (!packet || len < 2) return;
    u8 pkt_type = packet[0];

    if (pkt_type == HCI_EVENT_PKT) {
        const hci_event_hdr_t* evt = (const hci_event_hdr_t*)&packet[1];
        if (len < (2 + evt->param_len)) return;
        const u8* params = &packet[1 + sizeof(hci_event_hdr_t)];

        if (evt->event_code == HCI_EVT_INQUIRY_RESULT) {
            u8 num_responses = params[0];
            const u8* ptr = &params[1];
            for (u8 r = 0; r < num_responses; r++) {
                bd_addr_t addr;
                memcpy(addr.b, ptr, 6);
                u32 cod = (u32)ptr[9] | ((u32)ptr[10] << 8) | ((u32)ptr[11] << 16);
                record_discovered_device(&addr, "Classic Bluetooth Node", cod, -55, BT_TYPE_BREDR);
                ptr += 14; /* 6 (addr) + 1 (page_scan) + 2 (reserved) + 3 (cod) + 2 (clock_offset) */
            }
        } else if (evt->event_code == HCI_EVT_LE_META) {
            u8 subevent = params[0];
            if (subevent == 0x02) { /* LE Advertising Report */
                u8 num_reports = params[1];
                const u8* ptr = &params[2];
                for (u8 r = 0; r < num_reports; r++) {
                    bd_addr_t addr;
                    memcpy(addr.b, &ptr[2], 6);
                    u8 data_len = ptr[8];
                    i8 rssi = (i8)ptr[9 + data_len];
                    record_discovered_device(&addr, "BLE Peripheral", 0x001F00, rssi, BT_TYPE_LE);
                    ptr += (10 + data_len);
                }
            }
        } else if (evt->event_code == HCI_EVT_CONN_COMPLETE) {
            u8 status = params[0];
            u16 handle = (u16)params[1] | ((u16)params[2] << 8);
            if (status == HCI_SUCCESS) {
                u32 flags = spinlock_lock_irqsave(&s_bt_lock);
                for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
                    if (s_bt.connections[i].handle == handle || s_bt.connections[i].state == BT_DEV_CONNECTING) {
                        s_bt.connections[i].handle = handle;
                        s_bt.connections[i].state = BT_DEV_CONNECTED;
                        break;
                    }
                }
                spinlock_unlock_irqrestore(&s_bt_lock, flags);
            }
        }
    } else if (pkt_type == HCI_ACL_DATA_PKT) {
        const hci_acl_hdr_t* acl = (const hci_acl_hdr_t*)&packet[1];
        u16 handle = acl->handle_flags & 0x0FFF;
        u16 data_len = acl->data_len;
        const u8* payload = &packet[1 + sizeof(hci_acl_hdr_t)];

        u32 flags = spinlock_lock_irqsave(&s_bt_lock);
        for (u32 i = 0; i < BT_MAX_CONNECTIONS; i++) {
            if (s_bt.connections[i].handle == handle) {
                s_bt.connections[i].rx_packets++;
                s_bt.connections[i].rx_bytes += (len);
                break;
            }
        }
        spinlock_unlock_irqrestore(&s_bt_lock, flags);

        /* L2CAP Parsing */
        if (data_len >= sizeof(l2cap_hdr_t)) {
            const l2cap_hdr_t* l2cap = (const l2cap_hdr_t*)payload;
            if (l2cap->cid == L2CAP_CID_ATT) {
                /* Process Incoming ATT Read/Write */
                u8 att_op = payload[sizeof(l2cap_hdr_t)];
                if (att_op == ATT_OP_READ_REQ) {
                    u16 attr_handle = (u16)payload[sizeof(l2cap_hdr_t) + 1] | ((u16)payload[sizeof(l2cap_hdr_t) + 2] << 8);
                    for (u32 a = 0; a < s_bt.gatt_attrs_count; a++) {
                        if (s_bt.gatt_table[a].handle == attr_handle) {
                            u8 rsp[68];
                            rsp[0] = ATT_OP_READ_RSP;
                            for (u16 k = 0; k < s_bt.gatt_table[a].value_len; k++) {
                                rsp[1 + k] = s_bt.gatt_table[a].value[k];
                            }
                            bluetooth_send_acl(handle, L2CAP_CID_ATT, rsp, (u16)(1 + s_bt.gatt_table[a].value_len));
                            break;
                        }
                    }
                }
            }
        }
    }
}

void bluetooth_tick(void) {
    /* Periodic house-keeping: check connection health & scanning timers */
}

void bluetooth_get_status(bluetooth_subsystem_status_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_bt_lock);
    *out = s_bt;
    spinlock_unlock_irqrestore(&s_bt_lock, flags);
}
