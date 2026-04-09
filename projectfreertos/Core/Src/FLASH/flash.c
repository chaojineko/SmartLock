#include "flash.h"
#include "spi.h"
#include "rc522.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

#ifndef FLASH_CS_Pin
#define FLASH_CS_Pin GPIO_PIN_14
#define FLASH_CS_GPIO_Port GPIOB
#endif

#define FLASH_CMD_WRITE_ENABLE      0x06U
#define FLASH_CMD_READ_STATUS1      0x05U
#define FLASH_CMD_READ_DATA         0x03U
#define FLASH_CMD_PAGE_PROGRAM      0x02U
#define FLASH_CMD_SECTOR_ERASE_4K   0x20U
#define FLASH_CMD_JEDEC_ID          0x9FU

#define FLASH_STATUS_BUSY_MASK      0x01U

#define FLASH_PAGE_SIZE             256U
#define FLASH_SECTOR_SIZE           4096U

/* Reserve one sector for lock data. Adjust if this overlaps your own flash map. */
#define LOCK_DATA_ADDR              0x00010000UL

#define LOCK_DATA_MAGIC             0x4B434F4CUL /* 'LOCK' */
#define LOCK_DATA_VERSION           0x01U
#define LOCK_DATA_TOTAL_BYTES       64U
#define LOCK_DATA_CRC_OFFSET        60U

static void flash_select(void)
{
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
}

static void flash_deselect(void)
{
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
}

static uint8_t flash_spi_txrx(uint8_t tx)
{
	uint8_t rx = 0U;
	(void)HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1U, 100U);
	return rx;
}

static uint8_t flash_read_status1(void)
{
	uint8_t status;
	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_READ_STATUS1);
	status = flash_spi_txrx(0xFFU);
	flash_deselect();
	return status;
}

static uint8_t flash_wait_ready(uint32_t timeout_ms)
{
	uint32_t start = HAL_GetTick();
	while ((flash_read_status1() & FLASH_STATUS_BUSY_MASK) != 0U)
	{
		if ((HAL_GetTick() - start) > timeout_ms)
		{
			return 0U;
		}
		HAL_Delay(1U);
	}
	return 1U;
}

static void flash_write_enable(void)
{
	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_WRITE_ENABLE);
	flash_deselect();
}

static void flash_read_bytes(uint32_t addr, uint8_t *buf, uint16_t len)
{
	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_READ_DATA);
	(void)flash_spi_txrx((uint8_t)((addr >> 16) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)((addr >> 8) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)(addr & 0xFFU));

	for (uint16_t i = 0U; i < len; i++)
	{
		buf[i] = flash_spi_txrx(0xFFU);
	}
	flash_deselect();
}

static uint8_t flash_page_program(uint32_t addr, const uint8_t *buf, uint16_t len)
{
	flash_write_enable();

	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_PAGE_PROGRAM);
	(void)flash_spi_txrx((uint8_t)((addr >> 16) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)((addr >> 8) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)(addr & 0xFFU));

	for (uint16_t i = 0U; i < len; i++)
	{
		(void)flash_spi_txrx(buf[i]);
	}
	flash_deselect();

	return flash_wait_ready(200U);
}

static uint8_t flash_erase_sector_4k(uint32_t addr)
{
	flash_write_enable();

	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_SECTOR_ERASE_4K);
	(void)flash_spi_txrx((uint8_t)((addr >> 16) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)((addr >> 8) & 0xFFU));
	(void)flash_spi_txrx((uint8_t)(addr & 0xFFU));
	flash_deselect();

	return flash_wait_ready(3000U);
}

static uint8_t flash_write_bytes(uint32_t addr, const uint8_t *buf, uint16_t len)
{
	uint16_t written = 0U;

	while (written < len)
	{
		uint16_t page_off = (uint16_t)((addr + written) % FLASH_PAGE_SIZE);
		uint16_t chunk = (uint16_t)(FLASH_PAGE_SIZE - page_off);

		if (chunk > (len - written))
		{
			chunk = (uint16_t)(len - written);
		}

		if (flash_page_program(addr + written, &buf[written], chunk) == 0U)
		{
			return 0U;
		}

		written = (uint16_t)(written + chunk);
	}

	return 1U;
}

static uint32_t flash_crc32(const uint8_t *data, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFUL;

	for (uint32_t i = 0UL; i < len; i++)
	{
		crc ^= data[i];
		for (uint8_t j = 0U; j < 8U; j++)
		{
			if ((crc & 1UL) != 0UL)
			{
				crc = (crc >> 1) ^ 0xEDB88320UL;
			}
			else
			{
				crc >>= 1;
			}
		}
	}

	return ~crc;
}

uint8_t FlashStorage_Init(void)
{
	uint8_t id[3] = {0U, 0U, 0U};

	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_SET);

	flash_select();
	(void)flash_spi_txrx(FLASH_CMD_JEDEC_ID);
	id[0] = flash_spi_txrx(0xFFU);
	id[1] = flash_spi_txrx(0xFFU);
	id[2] = flash_spi_txrx(0xFFU);
	flash_deselect();

	if ((id[0] == 0x00U) || (id[0] == 0xFFU))
	{
		return 0U;
	}

	return 1U;
}

uint8_t FlashStorage_Load(LockPersistData_t *out_data)
{
	uint8_t raw[LOCK_DATA_TOTAL_BYTES];
	uint32_t magic;
	uint32_t crc_stored;
	uint32_t crc_calc;

	if (out_data == NULL)
	{
		return 0U;
	}

	memset(out_data, 0, sizeof(*out_data));
	flash_read_bytes(LOCK_DATA_ADDR, raw, LOCK_DATA_TOTAL_BYTES);

	magic = ((uint32_t)raw[0]) |
			((uint32_t)raw[1] << 8) |
			((uint32_t)raw[2] << 16) |
			((uint32_t)raw[3] << 24);

	if ((magic != LOCK_DATA_MAGIC) || (raw[4] != LOCK_DATA_VERSION))
	{
		return 0U;
	}

	crc_stored = ((uint32_t)raw[LOCK_DATA_CRC_OFFSET]) |
				 ((uint32_t)raw[LOCK_DATA_CRC_OFFSET + 1U] << 8) |
				 ((uint32_t)raw[LOCK_DATA_CRC_OFFSET + 2U] << 16) |
				 ((uint32_t)raw[LOCK_DATA_CRC_OFFSET + 3U] << 24);

	crc_calc = flash_crc32(raw, LOCK_DATA_CRC_OFFSET);
	if (crc_stored != crc_calc)
	{
		return 0U;
	}

	out_data->password_len = raw[5];
	if (out_data->password_len > LOCK_PASSWORD_MAX_LEN)
	{
		return 0U;
	}

	out_data->card_count = raw[6];
	if (out_data->card_count > LOCK_MAX_RFID_CARDS)
	{
		return 0U;
	}

	memcpy(out_data->password, &raw[8], LOCK_PASSWORD_MAX_LEN);
	out_data->password[out_data->password_len] = '\0';
	memcpy(out_data->cards, &raw[15], LOCK_MAX_RFID_CARDS * 4U);

	return 1U;
}

uint8_t FlashStorage_Save(const LockPersistData_t *in_data)
{
	uint8_t raw[LOCK_DATA_TOTAL_BYTES];
	uint32_t crc;

	if (in_data == NULL)
	{
		return 0U;
	}

	if ((in_data->password_len < 4U) || (in_data->password_len > LOCK_PASSWORD_MAX_LEN))
	{
		return 0U;
	}

	if (in_data->card_count > LOCK_MAX_RFID_CARDS)
	{
		return 0U;
	}

	memset(raw, 0xFF, sizeof(raw));

	raw[0] = (uint8_t)(LOCK_DATA_MAGIC & 0xFFU);
	raw[1] = (uint8_t)((LOCK_DATA_MAGIC >> 8) & 0xFFU);
	raw[2] = (uint8_t)((LOCK_DATA_MAGIC >> 16) & 0xFFU);
	raw[3] = (uint8_t)((LOCK_DATA_MAGIC >> 24) & 0xFFU);
	raw[4] = LOCK_DATA_VERSION;
	raw[5] = in_data->password_len;
	raw[6] = in_data->card_count;
	raw[7] = 0U;

	memcpy(&raw[8], in_data->password, LOCK_PASSWORD_MAX_LEN);
	memcpy(&raw[15], in_data->cards, LOCK_MAX_RFID_CARDS * 4U);

	crc = flash_crc32(raw, LOCK_DATA_CRC_OFFSET);
	raw[LOCK_DATA_CRC_OFFSET] = (uint8_t)(crc & 0xFFU);
	raw[LOCK_DATA_CRC_OFFSET + 1U] = (uint8_t)((crc >> 8) & 0xFFU);
	raw[LOCK_DATA_CRC_OFFSET + 2U] = (uint8_t)((crc >> 16) & 0xFFU);
	raw[LOCK_DATA_CRC_OFFSET + 3U] = (uint8_t)((crc >> 24) & 0xFFU);

	if (flash_erase_sector_4k(LOCK_DATA_ADDR) == 0U)
	{
		return 0U;
	}

	if (flash_write_bytes(LOCK_DATA_ADDR, raw, LOCK_DATA_TOTAL_BYTES) == 0U)
	{
		return 0U;
	}

	return 1U;
}
