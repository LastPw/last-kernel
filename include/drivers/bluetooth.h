#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "types.h"

/* ==============================================================================
 * Bluetooth HCI (Host Controller Interface) Packet Definitions
 * ============================================================================== */

#define HCI_COMMAND_PKT     0x01
#define HCI_ACL_DATA_PKT    0x02
#define HCI_SCO_DATA_PKT    0x03
#define HCI_EVENT_PKT       0x04
#define HCI_ISOC_DATA_PKT   0x05

/* Bluetooth 48-bit MAC Address */
typedef struct __attribute__((packed)) {
    u8 b[6];
} bd_addr_t;

/* HCI Command Header */
typedef struct __attribute__((packed)) {
    u16 opcode;         /* OCF (10 bits) | OGF (6 bits) */
    u8  param_len;
} hci_cmd_hdr_t;

/* HCI ACL Data Header */
typedef struct __attribute__((packed)) {
    u16 handle_flags;   /* Handle (12 bits) | PB flag (2 bits) | BC flag (2 bits) */
    u16 data_len;
} hci_acl_hdr_t;

/* HCI Event Header */
typedef struct __attribute__((packed)) {
    u8  event_code;
    u8  param_len;
} hci_event_hdr_t;

/* OGF (Opcode Group Fields) */
#define HCI_OGF_LINK_CTL        0x01
#define HCI_OGF_LINK_POLICY     0x02
#define HCI_OGF_HOST_CTL        0x03
#define HCI_OGF_INFO_PARAM      0x04
#define HCI_OGF_STATUS_PARAM    0x05
#define HCI_OGF_TESTING         0x06
#define HCI_OGF_LE_CTL          0x08
#define HCI_OGF_VENDOR          0x3F

#define HCI_OPCODE(ogf, ocf)    (((u16)(ogf) << 10) | ((u16)(ocf) & 0x03FF))

/* HCI Commands */
#define HCI_CMD_INQUIRY                 HCI_OPCODE(HCI_OGF_LINK_CTL, 0x0001)
#define HCI_CMD_INQUIRY_CANCEL          HCI_OPCODE(HCI_OGF_LINK_CTL, 0x0002)
#define HCI_CMD_CREATE_CONN             HCI_OPCODE(HCI_OGF_LINK_CTL, 0x0005)
#define HCI_CMD_DISCONNECT              HCI_OPCODE(HCI_OGF_LINK_CTL, 0x0006)
#define HCI_CMD_ACCEPT_CONN             HCI_OPCODE(HCI_OGF_LINK_CTL, 0x0009)
#define HCI_CMD_REJECT_CONN             HCI_OPCODE(HCI_OGF_LINK_CTL, 0x000A)
#define HCI_CMD_LINK_KEY_REQ_REPLY      HCI_OPCODE(HCI_OGF_LINK_CTL, 0x000B)
#define HCI_CMD_PIN_CODE_REQ_REPLY      HCI_OPCODE(HCI_OGF_LINK_CTL, 0x000D)
#define HCI_CMD_SET_EVENT_MASK          HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0001)
#define HCI_CMD_RESET                   HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0003)
#define HCI_CMD_WRITE_LOCAL_NAME        HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0013)
#define HCI_CMD_READ_LOCAL_NAME         HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0014)
#define HCI_CMD_WRITE_SCAN_ENABLE       HCI_OPCODE(HCI_OGF_HOST_CTL, 0x001A)
#define HCI_CMD_WRITE_CLASS_OF_DEV      HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0024)
#define HCI_CMD_WRITE_SIMPLE_PAIR_MODE  HCI_OPCODE(HCI_OGF_HOST_CTL, 0x0056)
#define HCI_CMD_READ_LOCAL_VERSION      HCI_OPCODE(HCI_OGF_INFO_PARAM, 0x0001)
#define HCI_CMD_READ_LOCAL_FEATURES     HCI_OPCODE(HCI_OGF_INFO_PARAM, 0x0003)
#define HCI_CMD_READ_BD_ADDR            HCI_OPCODE(HCI_OGF_INFO_PARAM, 0x0009)
#define HCI_CMD_LE_SET_EVENT_MASK       HCI_OPCODE(HCI_OGF_LE_CTL, 0x0001)
#define HCI_CMD_LE_READ_BUFFER_SIZE     HCI_OPCODE(HCI_OGF_LE_CTL, 0x0002)
#define HCI_CMD_LE_SET_SCAN_PARAM       HCI_OPCODE(HCI_OGF_LE_CTL, 0x000B)
#define HCI_CMD_LE_SET_SCAN_ENABLE      HCI_OPCODE(HCI_OGF_LE_CTL, 0x000C)
#define HCI_CMD_LE_CREATE_CONN          HCI_OPCODE(HCI_OGF_LE_CTL, 0x000D)

/* HCI Events */
#define HCI_EVT_INQUIRY_COMPLETE        0x01
#define HCI_EVT_INQUIRY_RESULT          0x02
#define HCI_EVT_CONN_COMPLETE           0x03
#define HCI_EVT_CONN_REQUEST            0x04
#define HCI_EVT_DISCONN_COMPLETE        0x05
#define HCI_EVT_AUTH_COMPLETE           0x06
#define HCI_EVT_REMOTE_NAME_REQ_COMPL   0x07
#define HCI_EVT_CMD_COMPLETE            0x0E
#define HCI_EVT_CMD_STATUS              0x0F
#define HCI_EVT_NUM_COMPL_PKTS          0x13
#define HCI_EVT_PIN_CODE_REQ            0x16
#define HCI_EVT_LINK_KEY_REQ            0x17
#define HCI_EVT_LINK_KEY_NOTIFY         0x18
#define HCI_EVT_INQUIRY_RESULT_WITH_RSSI 0x22
#define HCI_EVT_SIMPLE_PAIRING_COMPL    0x36
#define HCI_EVT_LE_META                 0x3E

/* HCI Status */
#define HCI_SUCCESS                     0x00
#define HCI_ERR_UNKNOWN_CONN_ID         0x02
#define HCI_ERR_AUTH_FAILURE            0x05
#define HCI_ERR_PIN_KEY_MISSING         0x06
#define HCI_ERR_MEM_CAPACITY_EXCEEDED   0x07
#define HCI_ERR_CONN_TIMEOUT            0x08
#define HCI_ERR_CONN_LIMIT_EXCEEDED     0x09
#define HCI_ERR_ACL_CONN_ALREADY_EXISTS 0x0B
#define HCI_ERR_COMMAND_DISALLOWED      0x0C
#define HCI_ERR_CONN_REJ_RESOURCES      0x0D
#define HCI_ERR_CONN_REJ_SECURITY       0x0E
#define HCI_ERR_CONN_REJ_BD_ADDR        0x0F
#define HCI_ERR_CONN_ACCEPT_TIMEOUT     0x10
#define HCI_ERR_UNSUPPORTED_PARAM       0x11
#define HCI_ERR_INVALID_HCI_PARAMS      0x12
#define HCI_ERR_REMOTE_USER_TERMINATED  0x13

/* ==============================================================================
 * Bluetooth L2CAP (Logical Link Control and Adaptation Protocol)
 * ============================================================================== */

#define L2CAP_CID_NULL          0x0000
#define L2CAP_CID_SIGNALING     0x0001
#define L2CAP_CID_CONNLESS      0x0002
#define L2CAP_CID_AMP_MANAGER   0x0003
#define L2CAP_CID_ATT           0x0004
#define L2CAP_CID_LE_SIGNALING  0x0005
#define L2CAP_CID_SMP           0x0006
#define L2CAP_CID_DYN_START     0x0040
#define L2CAP_CID_DYN_END       0xFFFF

/* L2CAP Header */
typedef struct __attribute__((packed)) {
    u16 length;
    u16 cid;
} l2cap_hdr_t;

/* L2CAP Signaling Command */
typedef struct __attribute__((packed)) {
    u8  code;
    u8  identifier;
    u16 length;
} l2cap_cmd_hdr_t;

#define L2CAP_CMD_COMMAND_REJ   0x01
#define L2CAP_CMD_CONN_REQ      0x02
#define L2CAP_CMD_CONN_RSP      0x03
#define L2CAP_CMD_CONF_REQ      0x04
#define L2CAP_CMD_CONF_RSP      0x05
#define L2CAP_CMD_DISCONN_REQ   0x06
#define L2CAP_CMD_DISCONN_RSP   0x07
#define L2CAP_CMD_INFO_REQ      0x0A
#define L2CAP_CMD_INFO_RSP      0x0B

/* Protocol / Service Multiplexers (PSM) */
#define L2CAP_PSM_SDP           0x0001
#define L2CAP_PSM_RFCOMM        0x0003
#define L2CAP_PSM_TCS_BIN       0x0005
#define L2CAP_PSM_BNEP          0x000F
#define L2CAP_PSM_HID_CTL       0x0011
#define L2CAP_PSM_HID_INT       0x0013
#define L2CAP_PSM_AVCTP         0x0017
#define L2CAP_PSM_AVDTP         0x0019

/* ==============================================================================
 * Bluetooth ATT / GATT Definitions (Bluetooth Low Energy / BLE)
 * ============================================================================== */

#define ATT_OP_ERROR_RSP            0x01
#define ATT_OP_EXCHANGE_MTU_REQ     0x02
#define ATT_OP_EXCHANGE_MTU_RSP     0x03
#define ATT_OP_FIND_INFO_REQ        0x04
#define ATT_OP_FIND_INFO_RSP        0x05
#define ATT_OP_READ_BY_TYPE_REQ     0x08
#define ATT_OP_READ_BY_TYPE_RSP     0x09
#define ATT_OP_READ_REQ             0x0A
#define ATT_OP_READ_RSP             0x0B
#define ATT_OP_READ_BY_GROUP_REQ    0x10
#define ATT_OP_READ_BY_GROUP_RSP    0x11
#define ATT_OP_WRITE_REQ            0x12
#define ATT_OP_WRITE_RSP            0x13
#define ATT_OP_WRITE_CMD            0x52
#define ATT_OP_HANDLE_VALUE_NOTIF   0x1B
#define ATT_OP_HANDLE_VALUE_IND     0x1D

/* Standard GATT Services UUIDs */
#define GATT_UUID_GENERIC_ACCESS    0x1800
#define GATT_UUID_GENERIC_ATTRIBUTE 0x1801
#define GATT_UUID_IMMEDIATE_ALERT   0x1802
#define GATT_UUID_DEVICE_INFO       0x180A
#define GATT_UUID_BATTERY_SERVICE   0x180F
#define GATT_UUID_HEART_RATE        0x180D
#define GATT_UUID_HID_SERVICE       0x1812

/* ==============================================================================
 * Bluetooth Audio (A2DP / SBC Codec) Stream Definitions
 * ============================================================================== */

typedef struct __attribute__((packed)) {
    u8  rtp_header[12];
    u8  sbc_frames_count;
    /* SBC frame data follows */
} a2dp_sbc_media_packet_t;

/* ==============================================================================
 * Device & Connection State Structures
 * ============================================================================== */

#define BT_MAX_DEVICES      32
#define BT_MAX_CONNECTIONS  8
#define BT_MAX_GATT_ATTRS   64

typedef enum {
    BT_DEV_DISCONNECTED = 0,
    BT_DEV_CONNECTING   = 1,
    BT_DEV_CONNECTED    = 2,
    BT_DEV_AUTHENTICATED= 3,
    BT_DEV_PAIRING      = 4
} bt_conn_state_t;

typedef enum {
    BT_TYPE_BREDR = 1,
    BT_TYPE_LE    = 2,
    BT_TYPE_DUAL  = 3
} bt_device_type_t;

typedef struct {
    bd_addr_t       addr;
    char            name[64];
    u32             class_of_device;
    i8              rssi;
    u8              paired;
    u8              link_key[16];
    bt_device_type_t dev_type;
    u32             last_seen_tick;
} bt_remote_device_t;

typedef struct {
    u16             handle;
    bd_addr_t       remote_addr;
    bt_conn_state_t state;
    u16             acl_mtu;
    u16             local_cid;
    u16             remote_cid;
    u16             psm;
    u32             rx_packets;
    u32             tx_packets;
    u32             rx_bytes;
    u32             tx_bytes;
} bt_connection_t;

typedef struct {
    u16             handle;
    u16             uuid16;
    u8              permissions;
    u16             value_len;
    u8              value[64];
} gatt_attribute_t;

typedef struct {
    u8              initialized;
    u8              scanning;
    bd_addr_t       local_addr;
    char            local_name[64];
    u16             hci_version;
    u16             manufacturer;
    u32             total_devices_discovered;
    u32             active_connections_count;
    bt_remote_device_t devices[BT_MAX_DEVICES];
    bt_connection_t    connections[BT_MAX_CONNECTIONS];
    gatt_attribute_t   gatt_table[BT_MAX_GATT_ATTRS];
    u32             gatt_attrs_count;
} bluetooth_subsystem_status_t;

/* ==============================================================================
 * Subsystem API Functions
 * ============================================================================== */

void bluetooth_init(void);
void bluetooth_tick(void);
int  bluetooth_start_scan(u8 le_scan);
int  bluetooth_stop_scan(void);
int  bluetooth_connect(const bd_addr_t* addr);
int  bluetooth_disconnect(u16 conn_handle);
int  bluetooth_send_acl(u16 conn_handle, u16 cid, const u8* data, u16 len);
int  bluetooth_send_audio_sbc(u16 conn_handle, const u8* pcm_audio, u16 samples);
void bluetooth_on_hci_packet(const u8* packet, u16 len);
int  bluetooth_register_gatt_service(u16 service_uuid);
int  bluetooth_register_gatt_char(u16 char_uuid, u8 permissions, const u8* initial_val, u16 val_len);
void bluetooth_get_status(bluetooth_subsystem_status_t* out);

/* ==============================================================================
 * Hardware Drivers Interface
 * ============================================================================== */

/* Intel Bluetooth Driver */
int bt_intel_init(void* usb_dev_handle);
int bt_intel_send_hci(const u8* buf, u16 len);
int bt_intel_download_firmware(const u8* patch_blob, u32 blob_size);
void bt_intel_poll(void);

/* Realtek Bluetooth Driver */
int bt_realtek_init(void* usb_dev_handle);
int bt_realtek_send_hci(const u8* buf, u16 len);
int bt_realtek_download_firmware(const u8* patch_blob, u32 blob_size);
void bt_realtek_poll(void);

#endif /* BLUETOOTH_H */
