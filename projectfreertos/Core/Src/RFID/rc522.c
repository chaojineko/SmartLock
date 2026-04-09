#include "main.h"
#include "stm32f4xx_hal.h"
#include "rc522.h"
#include <stdio.h>
#include <string.h>
#include "cmsis_os.h"

#include "spi.h"

extern SPI_HandleTypeDef hspi1;

// RFID��Ƭ�洢����ǰΪRAM�洢���ɺ����滻ΪFlash/EEPROM��
#define MAX_STORED_CARDS 10
static uint8_t stored_cards[MAX_STORED_CARDS][4];
static uint8_t stored_card_count = 0;

//SPI???????
void MF522SPI_Init(void)
{
	// ???SPI?????CubeMX??????????????????SPI??????????
	__HAL_RCC_SPI1_CLK_ENABLE();
}

// ??????SPI???��?��
uint8_t MF522SPI_ReadWriteByte(uint8_t TxData)
{
	uint8_t RxData;
	HAL_SPI_TransmitReceive(&hspi1, &TxData, &RxData, 1, 100);
	return RxData;
}


void MF522_Init(void)
{
	MF522SPI_Init();
	// NSS/CS pin should be high when idle
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_SET);
	// Reset the chip
	MFRC522_Reset();
}

void MF522SPI_Send(uint8_t val)
{ 
	MF522SPI_ReadWriteByte(
	val);
}
//
uint8_t MF522SPI_Recv(void)  
{ 
	uint8_t temp; 

	temp=MF522SPI_ReadWriteByte(0xFF);

	return temp; 
}

//????????????MFRC522?????????????????????
//?????????addr--??????????val--???????
void Write_MFRC522(uint8_t addr, uint8_t val) 
{
	//????????0XXXXXX0  
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET); // Ensure SPI flash deselected
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_RESET); // NSS = 0
	MF522SPI_Send((addr<<1)&0x7E);  
	MF522SPI_Send(val);  
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_SET); // NSS = 1
}
//????????????MFRC522?????????????????????
//?????????addr--????????
//?? ?? ???????????????????????  
uint8_t Read_MFRC522(uint8_t addr) 
{
	uint8_t val;
	//????????1XXXXXX0
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET); // Ensure SPI flash deselected
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_RESET); // NSS = 0
	MF522SPI_Send(((addr<<1)&0x7E)|0x80);
	val=MF522SPI_Recv();
	HAL_GPIO_WritePin(RC522_NSS_PORT, RC522_NSS_PIN, GPIO_PIN_SET); // NSS = 1
	return val;
}
//?????????????????????????
//????????????RC522???????
//?????????reg--????????;mask--?????
void SetBitMask(uint8_t reg, uint8_t mask)   
{     
	uint8_t tmp=0;
	//     
	tmp=Read_MFRC522(reg);     
	Write_MFRC522(reg,tmp|mask);  // set bit mask 
}
//????????????RC522???????
//?????????reg--????????;mask--?????
void ClearBitMask(uint8_t reg, uint8_t mask)   
{     
	uint8_t tmp=0;
	//     
	tmp=Read_MFRC522(reg);     
	Write_MFRC522(reg,tmp&(~mask));  //clear bit mask 
}
//??????????????????,????????????????????????????1ms????
void AntennaOn(void) 
{  
	uint8_t temp;
	//   
	temp=Read_MFRC522(TxControlReg);  
	if ((temp&0x03)==0)  
	{   
		SetBitMask(TxControlReg,0x03);  
	}
}
//?????????????????,????????????????????????????1ms????
void AntennaOff(void) 
{  
	ClearBitMask(TxControlReg,0x03);
}

//??????????????MFRC522
void MFRC522_Reset(void)
{
	// Ӳ����λ�����������ֲᣬ����RST����100ns
	HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_RESET); // RST = 0
	osDelay(1); // 1ms > 100ns
	HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_SET);   // RST = 1, �˳���λ
	osDelay(2); // �ȴ�оƬ�ȶ�

	// ����λ
	Write_MFRC522(CommandReg, PCD_RESETPHASE);

	// �ȴ�����λ���
	osDelay(2);

	//Timer: TPrescaler*TreloadVal/6.78MHz = 0xD3E*0x32/6.78=25ms
	Write_MFRC522(TModeReg,0x8D);				//TAuto=1��MFin�ߵ�ƽ������ʱ����MFin�͵�ƽ�Զ�ֹͣ
	Write_MFRC522(TPrescalerReg,0x3E); 	//Ԥ��Ƶֵ��8λ
	Write_MFRC522(TReloadRegL,0x32);		//��ʱ������ֵ��8λ
	Write_MFRC522(TReloadRegH,0x00);		//��ʱ������ֵ��8λ
	Write_MFRC522(TxAutoReg,0x40); 			//100%ASK
	Write_MFRC522(ModeReg,0x3D); 				//CRC��ʼֵ0x6363
	Write_MFRC522(CommandReg,0x00);			//���CommandReg
	AntennaOn();          							//������
}
//

//??????????RC522??ISO14443????
//?????????command--MF522??????
//					sendData--???RC522??????????????
//					sendLen--????????????
//					BackData--?????????????????
//					BackLen--???????????????
//?? ?? ??????????MI_O
u8 MFRC522_ToCard(u8 command, u8 *sendData, u8 sendLen, u8 *backData, u16 *backLen) 
{
	u8  status=MI_ERR;
	u8  irqEn=0x00;
	u8  waitIRq=0x00;
	u8  lastBits;
	u8  n;
	u16 i;
	//????????????????
	switch (command)     
	{         
		case PCD_AUTHENT:  		//???????   
			irqEn 	= 0x12;			//    
			waitIRq = 0x10;			//    
			break;
		case PCD_TRANSCEIVE: 	//????FIFO??????      
			irqEn 	= 0x77;			//    
			waitIRq = 0x30;			//    
			break;      
		default:    
			break;     
	}
	//
	Write_MFRC522(ComIEnReg, irqEn|0x80);		//????????????     
	ClearBitMask(ComIrqReg, 0x80);  				//?????????????????               	
	SetBitMask(FIFOLevelReg, 0x80);  				//FlushBuffer=1, FIFO?????
	Write_MFRC522(CommandReg, PCD_IDLE); 		//?MFRC522????   
	//??FIFO??????????     
	for (i=0; i<sendLen; i++)
		Write_MFRC522(FIFODataReg, sendData[i]);
	//???????
	Write_MFRC522(CommandReg, command);
	//???????????     
	if (command == PCD_TRANSCEIVE)					//??????????????MFRC522????????????????      
		SetBitMask(BitFramingReg, 0x80);  		//StartSend=1,transmission of data starts      
	//??????????????     
	// �ȴ�ͨѶ���
	i = 25; // ���ó�ʱʱ��Ϊ25ms
	do
	{
		n = Read_MFRC522(ComIrqReg);
		i--;
		if (i > 0) {
			osDelay(1); // ʹ��osDelay�ó�CPU������æ��
		}
	}while ((i!=0) && !(n&0x01) && !(n&waitIRq));
	//??????
	ClearBitMask(BitFramingReg, 0x80);   		//StartSend=0
	//?????25ms???????
	if (i != 0)
	{
		if(!(Read_MFRC522(ErrorReg) & 0x1B)) //BufferOvfl Collerr CRCErr ProtecolErr         
		{
			if (n & irqEn & 0x01)
				status = MI_NOTAGERR;
			//
			if (command == PCD_TRANSCEIVE)             
			{                 
				n = Read_MFRC522(FIFOLevelReg);		//n=0x02
				lastBits = Read_MFRC522(ControlReg) & 0x07;	//lastBits=0               
				if (lastBits!=0)                         
					*backLen = (n-1)*8 + lastBits; 
				else
					*backLen = n*8;									//backLen=0x10=16
				//
				if (n == 0)                         
				 	n = 1;                        
				if (n > MAX_LEN)         
				 	n = MAX_LEN;
				//
				for (i=0; i<n; i++)                 
					backData[i] = Read_MFRC522(FIFODataReg); 
			}
			//
			status = MI_OK;		
		}
		else
			status = MI_ERR;
	}	
	//
//	Write_MFRC522(ControlReg,0x80);				//timer stops     
//	Write_MFRC522(CommandReg, PCD_IDLE);	//
	//
	return status;
}
//?????????????????????????
//?????????reqMode--??????
//					TagType--??????????
//					0x4400 = Mifare_UltraLight
//					0x0400 = Mifare_One(S50)
//					0x0200 = Mifare_One(S70)
//					0x0800 = Mifare_Pro(X)
//					0x4403 = Mifare_DESFire
//?? ?? ??????????MI_OK	
u8 MFRC522_Request(u8 reqMode, u8 *TagType)
{  
	u8  status;    
	u16 backBits;   //???????????????
	//   
	Write_MFRC522(BitFramingReg, 0x07);  //TxLastBists = BitFramingReg[2..0]   
	TagType[0] = reqMode;  
	status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits); 
	// 
	if ((status != MI_OK) || (backBits != 0x10))  
	{       
		status = MI_ERR;
	}
	//  
	return status; 
}
//?????????????????????????????????????
//?????????serNum--????4??????????,??5???????????
//?? ?? ??????????MI_OK
u8 MFRC522_Anticoll(u8 *serNum) 
{     
	u8  status;     
	u8  i;     
	u8  serNumCheck=0;     
	u16 unLen;
	//           
	ClearBitMask(Status2Reg, 0x08);  			//TempSensclear     
	ClearBitMask(CollReg,0x80);   				//ValuesAfterColl  
	Write_MFRC522(BitFramingReg, 0x00);  	//TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL1;     
	serNum[1] = 0x20;     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	//      
	if (status == MI_OK)
	{   
		//??????????   
		for(i=0;i<4;i++)   
			serNumCheck^=serNum[i];
		//
		if(serNumCheck!=serNum[i])        
			status=MI_ERR;
	}
	SetBitMask(CollReg,0x80);  //ValuesAfterColl=1
	//      
	return status;
}
//????????????MF522????CRC
//?????????pIndata--?????CRC????????len--?????????pOutData--?????CRC???
void CalulateCRC(u8 *pIndata, u8 len, u8 *pOutData) 
{     
	u16 i;
	u8  n;
	//      
	ClearBitMask(DivIrqReg, 0x04);   			//CRCIrq = 0     
	SetBitMask(FIFOLevelReg, 0x80);   		//??FIFO???     
	Write_MFRC522(CommandReg, PCD_IDLE);   
	//??FIFO??????????      
	for (i=0; i<len; i++)
		Write_MFRC522(FIFODataReg, *(pIndata+i));
	//???RCR????
	Write_MFRC522(CommandReg, PCD_CALCCRC);
	//???CRC???????     
	i = 1000;     
	do      
	{         
		n = Read_MFRC522(DivIrqReg);         
		i--;     
	}while ((i!=0) && !(n&0x04));   //CRCIrq = 1
	//???CRC??????     
	pOutData[0] = Read_MFRC522(CRCResultRegL);     
	pOutData[1] = Read_MFRC522(CRCResultRegH);
	Write_MFRC522(CommandReg, PCD_IDLE);
}
//?????????????????????????????
//?????????serNum--??????????
//?? ?? ???????????????
u8 MFRC522_SelectTag(u8 *serNum) 
{     
	u8  i;     
	u8  status;     
	u8  size;     
	u16 recvBits;     
	u8  buffer[9];
	//     
	buffer[0] = PICC_ANTICOLL1;	//?????1     
	buffer[1] = 0x70;
	buffer[6] = 0x00;						     
	for (i=0; i<4; i++)					
	{
		buffer[i+2] = *(serNum+i);	//buffer[2]-buffer[5]?????????
		buffer[6]  ^=	*(serNum+i);	//????????
	}
	//
	CalulateCRC(buffer, 7, &buffer[7]);	//buffer[7]-buffer[8]?RCR??????
	ClearBitMask(Status2Reg,0x08);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
	//
	if ((status == MI_OK) && (recvBits == 0x18))    
		size = buffer[0];     
	else    
		size = 0;
	//	     
	return size; 
}
//?????????????????????
//?????????authMode--?????????
//					0x60 = ???A???
//					0x61 = ???B???
//					BlockAddr--????
//					Sectorkey--????????
//					serNum--???????????4???
//?? ?? ??????????MI_OK
u8 MFRC522_Auth(u8 authMode, u8 BlockAddr, u8 *Sectorkey, u8 *serNum) 
{     
	u8  status;     
	u16 recvBits;     
	u8  i;  
	u8  buff[12];    
	//?????+????+????????+????????     
	buff[0] = authMode;		//?????     
	buff[1] = BlockAddr;	//????     
	for (i=0; i<6; i++)
		buff[i+2] = *(Sectorkey+i);	//????????
	//
	for (i=0; i<4; i++)
		buff[i+8] = *(serNum+i);		//????????
	//
	status = MFRC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);
	//      
	if ((status != MI_OK) || (!(Read_MFRC522(Status2Reg) & 0x08)))
		status = MI_ERR;
	//
	return status;
}
//??????????????????
//?????????blockAddr--????;recvData--???????????
//?? ?? ??????????MI_OK
u8 MFRC522_Read(u8 blockAddr, u8 *recvData) 
{     
	u8  status;     
	u16 unLen;
	//      
	recvData[0] = PICC_READ;     
	recvData[1] = blockAddr;     
	CalulateCRC(recvData,2, &recvData[2]);     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);
	//
	if ((status != MI_OK) || (unLen != 0x90))
		status = MI_ERR;
	//
	return status;
}
//??????????????????
//?????????blockAddr--????;writeData--?????16???????
//?? ?? ??????????MI_OK
u8 MFRC522_Write(u8 blockAddr, u8 *writeData) 
{     
	u8  status;     
	u16 recvBits;     
	u8  i;  
	u8  buff[18];
	//           
	buff[0] = PICC_WRITE;     
	buff[1] = blockAddr;     
	CalulateCRC(buff, 2, &buff[2]);     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	//
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
		status = MI_ERR;
	//
	if (status == MI_OK)     
	{         
		for (i=0; i<16; i++)  //??FIFO??16Byte????                     
			buff[i] = *(writeData+i);
		//                     
		CalulateCRC(buff, 16, &buff[16]);         
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);           
		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))               
			status = MI_ERR;         
	}          
	return status;
}
//?????????????????????????
void MFRC522_Halt(void)
{
	u16 unLen;
	u8  buff[4];
	buff[0] = PICC_HALT;
	buff[1] = 0;
	CalulateCRC(buff, 2, &buff[2]);
	MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff,&unLen);
}

// RFID?????????
void RFID_Init(void)
{
	MF522_Init();
	uint8_t version = Read_MFRC522(VersionReg);
	printf("RC522 VersionReg: 0x%02X\r\n", version);
	if ((version == 0x00) || (version == 0xFF))
	{
		printf("RC522 SPI communication abnormal, check wiring/clock.\r\n");
	}
}
 
// ???RFID??
uint8_t RFID_VerifyCard(uint8_t *cardID)
{
	uint8_t status;
	uint8_t cardType[2];
	uint8_t serial[5];
	uint8_t card_size;

	// ???
	status = MFRC522_Request(PICC_REQALL, cardType);
	if (status != MI_OK) return 0;

	// ?????
	status = MFRC522_Anticoll(serial);
	if (status != MI_OK) return 0;

	// ???
	card_size = MFRC522_SelectTag(serial);
	if (card_size == 0) return 0;

	memcpy(cardID, serial, 4);

	return 1;
}

// ????RFID??
uint8_t RFID_AddCard(uint8_t *cardID)
{
	uint8_t status;
	uint8_t cardType[2];
	uint8_t serial[5];
	uint8_t card_size;
	
	// ???
	status = MFRC522_Request(PICC_REQALL, cardType);
	if (status != MI_OK) return 0;
	
	// ?????
	status = MFRC522_Anticoll(serial);
	if (status != MI_OK) return 0;
	
	// ???
	card_size = MFRC522_SelectTag(serial);
	if (card_size == 0) return 0;

	memcpy(cardID, serial, 4);
	
	// ????????????????????Flash??EEPROM?????
	printf("Card added successfully! ID: %02X%02X%02X%02X\r\n",
		cardID[0], cardID[1], cardID[2], cardID[3]);
	
	return 1;
}
// ?????????RFID??
uint8_t RFID_VerifyStoredCard(uint8_t *cardID)
{
	for (uint8_t i = 0; i < stored_card_count; i++)
	{
		if (memcmp(stored_cards[i], cardID, 4) == 0)
		{
			printf("Card verified successfully! ID: %02X%02X%02X%02X\r\n",
				cardID[0], cardID[1], cardID[2], cardID[3]);
			return 1;
		}
	}

	return 0;
}
// ?????????ID
uint8_t RFID_GetCurrentCardID(uint8_t *cardID)
{
	uint8_t status;
	uint8_t cardType[2];
	uint8_t serial[5];
	uint8_t card_size;
	
	// ???
	status = MFRC522_Request(PICC_REQALL, cardType);
	if (status != MI_OK) return 0;
	
	// ?????
	status = MFRC522_Anticoll(serial);
	if (status != MI_OK) return 0;
	
	// ???
	card_size = MFRC522_SelectTag(serial);
	if (card_size == 0) return 0;

	memcpy(cardID, serial, 4);
	
	return 1;
}
// RFID???????
void RFID_Test(void)
{
	uint8_t card_id[4];
	uint8_t status;

	printf("RFID Test Started...\r\n");

	status = RFID_VerifyCard(card_id);
	if (status)
	{
		printf("Card detected! ID: %02X%02X%02X%02X\r\n",
			card_id[0], card_id[1], card_id[2], card_id[3]);
	}
	else
	{
		printf("No card detected\r\n");
	}
}
// ???????????
uint8_t RFID_StoreCard(uint8_t *cardID)
{
	if (stored_card_count >= MAX_STORED_CARDS)
	{
		printf("Card storage full!\r\n");
		return 0;
	}
	
	// ????????????
	for (uint8_t i = 0; i < stored_card_count; i++)
	{
		if (memcmp(stored_cards[i], cardID, 4) == 0)
		{
			printf("Card already exists!\r\n");
			return 0;
		}
	}
	
	// ????????
	memcpy(stored_cards[stored_card_count], cardID, 4);
	stored_card_count++;
	
	printf("Card stored successfully! Total: %d\r\n", stored_card_count);
	return 1;
}

uint8_t RFID_RemoveStoredCard(uint8_t *cardID)
{
	if ((cardID == NULL) || (stored_card_count == 0U))
	{
		return 0;
	}

	for (uint8_t i = 0; i < stored_card_count; i++)
	{
		if (memcmp(stored_cards[i], cardID, 4) == 0)
		{
			for (uint8_t j = i; j + 1U < stored_card_count; j++)
			{
				memcpy(stored_cards[j], stored_cards[j + 1U], 4);
			}

			memset(stored_cards[stored_card_count - 1U], 0, 4);
			stored_card_count--;
			printf("Card removed! Total: %d\r\n", stored_card_count);
			return 1;
		}
	}

	printf("Card not found!\r\n");
	return 0;
}
// ??????????????? - ????????????
// ??????????????
uint8_t RFID_GetStoredCardCount(void)
{
	return stored_card_count;
}
// ?????????????
void RFID_ClearStoredCards(void)
{
	stored_card_count = 0;
	printf("All stored cards cleared!\r\n");
}

void RFID_ExportStoredCards(uint8_t *flat_cards, uint8_t max_cards, uint8_t *out_count)
{
	uint8_t copy_count;

	if ((flat_cards == NULL) || (out_count == NULL) || (max_cards == 0U))
	{
		return;
	}

	copy_count = stored_card_count;
	if (copy_count > max_cards)
	{
		copy_count = max_cards;
	}

	memcpy(flat_cards, stored_cards, (uint32_t)copy_count * 4U);
	*out_count = copy_count;
}

void RFID_ImportStoredCards(const uint8_t *flat_cards, uint8_t count)
{
	if (flat_cards == NULL)
	{
		return;
	}

	if (count > MAX_STORED_CARDS)
	{
		count = MAX_STORED_CARDS;
	}

	memset(stored_cards, 0, sizeof(stored_cards));
	memcpy(stored_cards, flat_cards, (uint32_t)count * 4U);
	stored_card_count = count;
}
// RFID???????
void RFID_Demo(void)
{
	uint8_t card_id[4];
	uint8_t status;
	
	printf("\r\n========================================\r\n");
	printf("RFID Demo Started!\r\n");
	printf("Please place an RFID card near the reader...\r\n");
	printf("========================================\r\n\r\n");
	
	// ???1??????
	status = RFID_GetCurrentCardID(card_id);
	if (status)
	{
		printf("? Card detected! ID: %02X%02X%02X%02X\r\n",
			card_id[0], card_id[1], card_id[2], card_id[3]);
		
		// ???2?????????????
		if (RFID_StoreCard(card_id))
		{
			printf("? Card added to storage successfully!\r\n");
			printf("  Total stored cards: %d\r\n", RFID_GetStoredCardCount());
			
			// ???3????????????
			if (RFID_VerifyStoredCard(card_id))
			{
				printf("? Card verification successful!\r\n");
			}
			else
			{
				printf("? Card verification failed!\r\n");
			}
		}
		else
		{
			printf("? Failed to add card to storage!\r\n");
		}
	}
	else
	{
		printf("? No card detected!\r\n");
		printf("  Please make sure the card is close to the RFID reader.\r\n");
	}
	
	printf("\r\n========================================\r\n");
	printf("RFID Demo Complete!\r\n");
	printf("Stored cards: %d\r\n", RFID_GetStoredCardCount());
	printf("========================================\r\n\r\n");
}
