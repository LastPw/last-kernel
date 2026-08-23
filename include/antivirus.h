#ifndef ANTIVIRUS_H
#define ANTIVIRUS_H

#include "types.h"

typedef struct {
    u32 active_threats;
    u32 quarantined_processes;
    u32 network_anomalies;
    u32 filesystem_integrity_percent;
    u32 signature_version;
    u32 heuristic_version;
    u32 scans_total;
    u32 infected_events;
} antivirus_status_t;

void antivirus_init(void);
void antivirus_tick(void);
int antivirus_scan_buffer(const u8* data, u32 len, u32 task_id);
void antivirus_on_file_event(const char* name, const u8* data, u32 len, u32 task_id);
void antivirus_on_network_event(u32 packet_len, u8 suspicious);
void antivirus_status(antivirus_status_t* out);

#endif
