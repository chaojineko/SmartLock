#ifndef __RC522_H__
#define __RC522_H__
#include "main.h"

// 类型定义 - 兼容旧代码
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// RC522引脚定义 - 适配项目配置
#define RC522_NSS_PIN    RFID_CS_Pin
#define RC522_NSS_PORT   RFID_CS_GPIO_Port
#define RC522_SCK_PIN    GPIO_PIN_3
#define RC522_SCK_PORT   GPIOB
#define RC522_MOSI_PIN   GPIO_PIN_5
#define RC522_MOSI_PORT  GPIOB
#define RC522_MISO_PIN   GPIO_PIN_4
#define RC522_MISO_PORT  GPIOB
#define RC522_RST_PIN    RFID_RST_Pin
#define RC522_RST_PORT   RFID_RST_GPIO_Port
// RC522引脚定义 - 适配项目配置
// SDA-PG7, SCK-PB3, MOSI-PB5, MISO-PB4, RST-PG8
// 函数声明
void MF522SPI_Init(void);
uint8_t MF522SPI_ReadWriteByte(uint8_t TxData);
void MF522_Init(void);
void MFRC522_Reset(void);
void Write_MFRC522(uint8_t addr, uint8_t val);
uint8_t Read_MFRC522(uint8_t addr);
void SetBitMask(uint8_t reg, uint8_t mask);
void ClearBitMask(uint8_t reg, uint8_t mask);
void AntennaOn(void);
void AntennaOff(void);

// RFID操作函数
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType);
uint8_t MFRC522_Anticoll(uint8_t *serNum);
uint8_t MFRC522_SelectTag(uint8_t *serNum);
uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum);
uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t *recvData);
uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t *writeData);
void MFRC522_Halt(void);

// 高级RFID操作函数
uint8_t RFID_VerifyCard(uint8_t *cardID);
uint8_t RFID_AddCard(uint8_t *cardID);
void RFID_Init(void);
uint8_t RFID_VerifyStoredCard(uint8_t *cardID);
uint8_t RFID_GetCurrentCardID(uint8_t *cardID);
void RFID_Test(void);
uint8_t RFID_StoreCard(uint8_t *cardID);
uint8_t RFID_RemoveStoredCard(uint8_t *cardID);
uint8_t RFID_GetStoredCardCount(void);
void RFID_ClearStoredCards(void);
void RFID_ExportStoredCards(uint8_t *flat_cards, uint8_t max_cards, uint8_t *out_count);
void RFID_ImportStoredCards(const uint8_t *flat_cards, uint8_t count);
void RFID_Demo(void);
/////////////////////////////////////////////////////////////////////
//MF522???
/////////////////////////////////////////////////////////////////////
#define PCD_IDLE              0x00               //??????
#define PCD_AUTHENT           0x0E               //????
#define PCD_RECEIVE           0x08               //????
#define PCD_TRANSMIT          0x04               //????
#define PCD_TRANSCEIVE        0x0C               //???????
#define PCD_RESETPHASE        0x0F               //??
#define PCD_CALCCRC           0x03               //CRC??

/////////////////////////////////////////////////////////////////////
//Mifare_One?????
/////////////////////////////////////////////////////////////////////
#define PICC_REQIDL           0x26               //????????????
#define PICC_REQALL           0x52               //????????
#define PICC_ANTICOLL1        0x93               //???
#define PICC_ANTICOLL2        0x95               //???
#define PICC_AUTHENT1A        0x60               //??A??
#define PICC_AUTHENT1B        0x61               //??B??
#define PICC_READ             0x30               //??
#define PICC_WRITE            0xA0               //??
#define PICC_DECREMENT        0xC0               //??
#define PICC_INCREMENT        0xC1               //??
#define PICC_RESTORE          0xC2               //????????
#define PICC_TRANSFER         0xB0               //????????
#define PICC_HALT             0x50               //??

/////////////////////////////////////////////////////////////////////
//MF522 FIFO????
/////////////////////////////////////////////////////////////////////
#define DEF_FIFO_LENGTH       64                 //FIFO size=64byte

/////////////////////////////////////////////////////////////////////
//MF522?????
/////////////////////////////////////////////////////////////////////
// PAGE 0
#define     RFU00                 0x00    
#define     CommandReg            0x01    
#define     ComIEnReg             0x02    
#define     DivlEnReg             0x03    
#define     ComIrqReg             0x04    
#define     DivIrqReg             0x05
#define     ErrorReg              0x06    
#define     Status1Reg            0x07    
#define     Status2Reg            0x08    
#define     FIFODataReg           0x09
#define     FIFOLevelReg          0x0A
#define     WaterLevelReg         0x0B
#define     ControlReg            0x0C
#define     BitFramingReg         0x0D
#define     CollReg               0x0E
#define     RFU0F                 0x0F
// PAGE 1     
#define     RFU10                 0x10
#define     ModeReg               0x11
#define     TxModeReg             0x12
#define     RxModeReg             0x13
#define     TxControlReg          0x14
#define     TxAutoReg             0x15
#define     TxSelReg              0x16
#define     RxSelReg              0x17
#define     RxThresholdReg        0x18
#define     DemodReg              0x19
#define     RFU1A                 0x1A
#define     RFU1B                 0x1B
#define     RFU1C             		0x1C
#define     RFU1D                 0x1D
#define     RFU1E                 0x1E
#define     SerialSpeedReg        0x1F
// PAGE 2    
#define     RFU20                 0x20  
#define     CRCResultRegH         0x21
#define     CRCResultRegL         0x22
#define     RFU23                 0x23
#define     ModWidthReg           0x24
#define     RFU25                 0x25
#define     RFCfgReg              0x26
#define     GsNReg                0x27
#define     CWGsCfgReg            0x28
#define     ModGsCfgReg           0x29
#define     TModeReg              0x2A
#define     TPrescalerReg         0x2B
#define     TReloadRegH           0x2C
#define     TReloadRegL           0x2D
#define     TCounterValueRegH     0x2E
#define     TCounterValueRegL     0x2F
// PAGE 3      
#define     RFU30                 0x30
#define     TestSel1Reg           0x31
#define     TestSel2Reg           0x32
#define     TestPinEnReg          0x33
#define     TestPinValueReg       0x34
#define     TestBusReg            0x35
#define     AutoTestReg           0x36
#define     VersionReg            0x37
#define     AnalogTestReg         0x38
#define     TestDAC1Reg           0x39  
#define     TestDAC2Reg           0x3A   
#define     TestADCReg            0x3B   
#define     RFU3C                 0x3C   
#define     RFU3D                 0x3D   
#define     RFU3E                 0x3E   
#define     RFU3F		  		  0x3F

/////////////////////////////////////////////////////////////////////
//?MF522??????????
/////////////////////////////////////////////////////////////////////
#define MI_OK                     0
#define MI_NOTAGERR               1
#define MI_ERR                    2
//
#define MAX_LEN					18

//
void STM32_SPI2_Init(void);
void STM32_SPI3_Init(void);
void SPI2_Send(uint8_t val);

void MF522SPI_Send(uint8_t val);
uint8_t MF522SPI_Recv(void);
void MFRC522_Initializtion(void);
void Write_MFRC522(uint8_t addr, uint8_t val);
uint8_t Read_MFRC522(uint8_t addr);
void SetBitMask(uint8_t reg, uint8_t mask);
void ClearBitMask(uint8_t reg, uint8_t mask);
void AntennaOn(void);
void AntennaOff(void);
void MFRC522_Reset(void);
void MFRC522_Init(void);
uint8_t MFRC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen);
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType);
uint8_t MFRC522_Anticoll(uint8_t *serNum);
void CalulateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData);
uint8_t MFRC522_SelectTag(uint8_t *serNum);
uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum);
uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t *recvData);
uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t *writeData);
void MFRC522_Halt(void); 

void MF522_Init(void);

#endif
