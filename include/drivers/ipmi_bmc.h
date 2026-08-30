#ifndef IPMI_BMC_H
#define IPMI_BMC_H

#include "types.h"

#define IPMI_KCS_DATA_PORT      0xCA2
#define IPMI_KCS_STATUS_PORT    0xCA3
#define IPMI_KCS_COMMAND_PORT   0xCA3

/* IPMI NetFn Codes */
#define IPMI_NETFN_CHASSIS      0x00
#define IPMI_NETFN_BRIDGE       0x02
#define IPMI_NETFN_SENSOR       0x04
#define IPMI_NETFN_APP          0x06
#define IPMI_NETFN_STORAGE      0x0A

/* IPMI Commands */
#define IPMI_CMD_GET_DEVICE_ID          0x01
#define IPMI_CMD_CHASSIS_CONTROL        0x02
#define IPMI_CMD_CHASSIS_STATUS         0x01
#define IPMI_CMD_GET_SDR                0x23
#define IPMI_CMD_SET_WATCHDOG_TIMER     0x24
#define IPMI_CMD_RESET_WATCHDOG_TIMER   0x22

typedef enum {
    CHASSIS_PWR_DOWN    = 0x00,
    CHASSIS_PWR_UP      = 0x01,
    CHASSIS_PWR_CYCLE   = 0x02,
    CHASSIS_HARD_RESET  = 0x03,
    CHASSIS_PULSE_DIAG  = 0x04,
    CHASSIS_SOFT_SHUTDOWN = 0x05
} ipmi_chassis_ctrl_t;

typedef struct {
    u8  device_id;
    u8  device_revision;
    u8  firmware_rev_major;
    u8  firmware_rev_minor;
    u8  ipmi_version;           /* 0x02 = IPMI 2.0 */
    u16 manufacturer_id;        /* IANA enterprise number */
    u16 product_id;
} ipmi_device_id_t;

typedef struct {
    u8  power_on;
    u8  power_overload;
    u8  interlock_asserted;
    u8  fault_detected;
    u8  chassis_identify_active;
} ipmi_chassis_status_t;

typedef struct {
    u8  bmc_present;
    u8  kcs_ready;
    u32 psu_count;
    u8  psu_redundant_healthy;
    u32 fan_count;
    u16 fan_rpm[8];
    u32 ecc_single_bit_errors;
    u32 ecc_multi_bit_errors;
    u32 watchdog_countdown_sec;
    ipmi_device_id_t dev_id;
    ipmi_chassis_status_t chassis;
} server_hardware_health_t;

/* IPMI Subsystem API */
void ipmi_bmc_init(void);
int ipmi_kcs_send_request(u8 netfn, u8 cmd, const u8* req_data, u8 req_len, u8* rsp_data, u8* rsp_len);
int ipmi_chassis_control(ipmi_chassis_ctrl_t action);
int ipmi_ping_watchdog(void);
void ipmi_bmc_poll(void);
void ipmi_get_hardware_health(server_hardware_health_t* out);

#endif /* IPMI_BMC_H */
