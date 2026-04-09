/*
 * SPI Flash persistence layer for smart lock data.
 */
#ifndef __LOCK_FLASH_H__
#define __LOCK_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define LOCK_PASSWORD_MAX_LEN   6U
#define LOCK_MAX_RFID_CARDS     10U

typedef struct
{
	uint8_t password_len;
	char password[LOCK_PASSWORD_MAX_LEN + 1U];
	uint8_t card_count;
	uint8_t cards[LOCK_MAX_RFID_CARDS][4];
} LockPersistData_t;

uint8_t FlashStorage_Init(void);
uint8_t FlashStorage_Load(LockPersistData_t *out_data);
uint8_t FlashStorage_Save(const LockPersistData_t *in_data);

#ifdef __cplusplus
}
#endif

#endif /* __LOCK_FLASH_H__ */
