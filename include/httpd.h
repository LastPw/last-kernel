#ifndef LASTOS_HTTPD_H
#define LASTOS_HTTPD_H

#include "types.h"

int httpd_start(u16 port, const char* root);
int httpd_start_tls(u16 port, const char* root);
int httpd_stop(void);
void httpd_poll(u32 budget);
int httpd_active(void);
int httpd_tls_active(void);
int httpd_set_proxy(const char* prefix, u32 ip, u16 port);
void httpd_clear_proxy(void);
void httpd_status(u16* out_port, u32* out_requests, u32* out_errors,
                  u8* out_proxy, char* out_root, u32 root_len);

#endif
