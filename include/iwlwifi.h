#ifndef IWLWIFI_H
#define IWLWIFI_H

#include "types.h"

void iwlwifi_init(void);
int iwlwifi_available(void);
int iwlwifi_connect(const char* ssid, const char* passphrase);

#endif /* IWLWIFI_H */
