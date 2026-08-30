#include "laptop_platform.h"
#include "spinlock.h"
#include "timer.h"
#include "screen.h"

#ifndef HOST_TEST
#include "io.h"
#define ec_inb inb
#define ec_outb outb
#else
extern u8 mock_ec_inb(u16 port);
extern void mock_ec_outb(u16 port, u8 val);
#define ec_inb mock_ec_inb
#define ec_outb mock_ec_outb
#endif

/* ==============================================================================
 * ACPI Embedded Controller (EC) Driver Implementation
 * ============================================================================== */

static spinlock_t s_ec_lock;
static u8 s_ec_burst_active = 0;
static u32 s_ec_events_count = 0;
static u8 s_ec_virtual_space[256]; /* Emulated/Cached EC RAM Space */

static int ec_wait_ibf_clear(void) {
    u32 timeout = 1000;
    while (timeout--) {
        u8 status = ec_inb(EC_CMDPORT);
        if ((status & EC_FLAG_IBF) == 0) return 0;
    }
    return -1;
}

static int ec_wait_obf_set(void) {
    u32 timeout = 1000;
    while (timeout--) {
        u8 status = ec_inb(EC_CMDPORT);
        if (status & EC_FLAG_OBF) return 0;
    }
    return -1;
}

void acpi_ec_init(void) {
    spinlock_init(&s_ec_lock);
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);

    s_ec_burst_active = 0;
    s_ec_events_count = 0;

    /* Initialize Default Virtual EC Space with standard battery and thermal tables */
    for (u32 i = 0; i < 256; i++) s_ec_virtual_space[i] = 0;
    s_ec_virtual_space[0x00] = 0x01; /* AC Connected */
    s_ec_virtual_space[0x01] = 0x64; /* Battery 100% */
    s_ec_virtual_space[0x02] = 0x2A; /* CPU Temp: 42 C */
    s_ec_virtual_space[0x03] = 0x00; /* Lid Open */

    spinlock_unlock_irqrestore(&s_ec_lock, flags);
}

int acpi_ec_read(u8 address, u8* data) {
    if (!data) return -1;
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);

    if (ec_wait_ibf_clear() != 0) {
        /* Fallback to EC RAM cache if hardware not responding in emulator */
        *data = s_ec_virtual_space[address];
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    ec_outb(EC_CMDPORT, EC_CMD_RD_EC);
    if (ec_wait_ibf_clear() != 0) {
        *data = s_ec_virtual_space[address];
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    ec_outb(EC_DATAPORT, address);
    if (ec_wait_obf_set() != 0) {
        *data = s_ec_virtual_space[address];
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    *data = ec_inb(EC_DATAPORT);
    s_ec_virtual_space[address] = *data;

    spinlock_unlock_irqrestore(&s_ec_lock, flags);
    return 0;
}

int acpi_ec_write(u8 address, u8 data) {
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);
    s_ec_virtual_space[address] = data;

    if (ec_wait_ibf_clear() != 0) {
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    ec_outb(EC_CMDPORT, EC_CMD_WR_EC);
    if (ec_wait_ibf_clear() != 0) {
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    ec_outb(EC_DATAPORT, address);
    if (ec_wait_ibf_clear() != 0) {
        spinlock_unlock_irqrestore(&s_ec_lock, flags);
        return 0;
    }

    ec_outb(EC_DATAPORT, data);
    spinlock_unlock_irqrestore(&s_ec_lock, flags);
    return 0;
}

int acpi_ec_burst_enable(void) {
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);
    s_ec_burst_active = 1;
    if (ec_wait_ibf_clear() == 0) {
        ec_outb(EC_CMDPORT, EC_CMD_BE_EC);
    }
    spinlock_unlock_irqrestore(&s_ec_lock, flags);
    return 0;
}

int acpi_ec_burst_disable(void) {
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);
    s_ec_burst_active = 0;
    if (ec_wait_ibf_clear() == 0) {
        ec_outb(EC_CMDPORT, EC_CMD_BD_EC);
    }
    spinlock_unlock_irqrestore(&s_ec_lock, flags);
    return 0;
}

u8 acpi_ec_query_event(void) {
    u32 flags = spinlock_lock_irqsave(&s_ec_lock);
    u8 evt = 0;

    if (ec_wait_ibf_clear() == 0) {
        ec_outb(EC_CMDPORT, EC_CMD_QR_EC);
        if (ec_wait_obf_set() == 0) {
            evt = ec_inb(EC_DATAPORT);
        }
    }
    if (evt != 0) s_ec_events_count++;

    spinlock_unlock_irqrestore(&s_ec_lock, flags);
    return evt;
}

void acpi_ec_poll(void) {
    u8 status = ec_inb(EC_CMDPORT);
    if (status & EC_FLAG_SCI_EVT) {
        u8 evt_code = acpi_ec_query_event();
        if (evt_code != 0) {
            /* Process ACPI SCI Event Notifications */
        }
    }
}
