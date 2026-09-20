#ifndef __HCI_H__
#define __HCI_H__

#include "stdint.h"
#include "hci_conf.h"
#include "bookoo_error_def.h"


typedef enum {
    HCI_BUZZER_STARTUP_REPONSE = 0,
    HCI_BUZZER_CLICK_RESPONSE,
    HCI_BUZZER_INCHARGE_RESPONSE,
    
    HCI_BUZZER_MELODY_TOTAL_COUNT
} HCI_BUZZER_MELODY_E;



SYSTEM_ERROR_CODE_E send_hci_buzzer_melody(uint16_t melody);

SYSTEM_ERROR_CODE_E send_hci_buzzer_gear(uint16_t gear);

#endif

