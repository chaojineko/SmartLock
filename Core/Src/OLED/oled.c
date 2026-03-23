 #include "oled.h"
#include "oledfont.h"
#include "stdlib.h"

#define Max_Column 128

// Forward declarations for static functions
static void delay_us(uint32_t us);
static void SDA_Set_Mode(uint32_t mode);
static void IIC_Start(void);
static void IIC_Stop(void);
static void IIC_Wait_Ack(void);
static void Write_IIC_Byte(uint8_t IIC_Byte);
static void Write_IIC_Command(uint8_t IIC_Command);
static void Write_IIC_Data(uint8_t IIC_Data);


// Microsecond delay function (busy-wait).
// Note: This is a blocking delay and should be used for very short periods only.
static void delay_us(uint32_t us)
{
    // A simple loop-based delay. Accuracy depends on compiler optimization and clock speed.
    volatile uint32_t i = us * (HAL_RCC_GetHCLKFreq() / 1000000 / 5);
    while (i--);
}

// Function to change SDA pin mode between Output and Input
static void SDA_Set_Mode(uint32_t mode)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = OLED_SDA_Pin;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    if (mode == GPIO_MODE_OUTPUT_OD) {
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
        GPIO_InitStruct.Pull = GPIO_NOPULL; // Use GPIO_PULLUP if you don't have external pull-ups
    } else { // GPIO_MODE_INPUT
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
    }
    HAL_GPIO_Init(OLED_SDA_GPIO_Port, &GPIO_InitStruct);
}


/**********************************************
// IIC Start
**********************************************/
static void IIC_Start(void)
{
    SDA_Set_Mode(GPIO_MODE_OUTPUT_OD);
    OLED_SDA_Set();
    OLED_SCL_Set();
    delay_us(5);
    OLED_SDA_Clr();
    delay_us(5);
    OLED_SCL_Clr();
    delay_us(5);
}

/**********************************************
// IIC Stop
**********************************************/
static void IIC_Stop(void)
{
    SDA_Set_Mode(GPIO_MODE_OUTPUT_OD);
    OLED_SDA_Clr();
    OLED_SCL_Set();
    delay_us(5);
    OLED_SDA_Set();
    delay_us(5);
}

/**********************************************
// IIC Wait Ack
**********************************************/
static void IIC_Wait_Ack(void)
{
    SDA_Set_Mode(GPIO_MODE_INPUT);
    OLED_SCL_Set();
    delay_us(5);
    // A robust implementation should check OLED_SDA_Read() and handle NACK.
    // Here we just wait for the clock cycle, mimicking the original code's behavior.
    OLED_SCL_Clr();
    delay_us(5);
}

/**********************************************
// IIC Write byte
**********************************************/
static void Write_IIC_Byte(uint8_t IIC_Byte)
{
    SDA_Set_Mode(GPIO_MODE_OUTPUT_OD);
    OLED_SCL_Clr(); // Pull down SCL to start data transfer

    for (int i = 7; i >= 0; i--)
    {
        if (IIC_Byte & (1 << i))
        {
            OLED_SDA_Set();
        }
        else
        {
            OLED_SDA_Clr();
        }
        delay_us(2);
        OLED_SCL_Set();
        delay_us(2);
        OLED_SCL_Clr();
        delay_us(2);
    }
}

/**********************************************
// IIC Write Command
**********************************************/
static void Write_IIC_Command(uint8_t IIC_Command)
{
    IIC_Start();
    Write_IIC_Byte(0x78); // Slave address, SA0=0
    IIC_Wait_Ack();
    Write_IIC_Byte(0x00); // Write command
    IIC_Wait_Ack();
    Write_IIC_Byte(IIC_Command);
    IIC_Wait_Ack();
    IIC_Stop();
}

/**********************************************
// IIC Write Data
**********************************************/
static void Write_IIC_Data(uint8_t IIC_Data)
{
    IIC_Start();
    Write_IIC_Byte(0x78); // Slave address, SA0=0
    IIC_Wait_Ack();
    Write_IIC_Byte(0x40); // Write data
    IIC_Wait_Ack();
    Write_IIC_Byte(IIC_Data);
    IIC_Wait_Ack();
    IIC_Stop();
}

void OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    if (cmd)
    {
        Write_IIC_Data(dat);
    }
    else
    {
        Write_IIC_Command(dat);
    }
}

/********************************************
// fill_Picture
********************************************/
void fill_picture(unsigned char fill_Data)
{
	unsigned char m,n;
	for(m=0;m<8;m++)
	{
		OLED_WR_Byte(0xb0+m,0);		//page0-page1
		OLED_WR_Byte(0x00,0);		//low column start address
		OLED_WR_Byte(0x10,0);		//high column start address
		for(n=0;n<128;n++)
			{
				OLED_WR_Byte(fill_Data,1);
			}
	}
}

//????????
void OLED_Set_Pos(unsigned char x, unsigned char y) 
{ 
    OLED_WR_Byte(0xb0+y,OLED_CMD);
	OLED_WR_Byte(((x&0xf0)>>4)|0x10,OLED_CMD);
	OLED_WR_Byte((x&0x0f),OLED_CMD); 
}   	  
//????OLED???    
void OLED_Display_On(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC????
	OLED_WR_Byte(0X14,OLED_CMD);  //DCDC ON
	OLED_WR_Byte(0XAF,OLED_CMD);  //DISPLAY ON
}
//???OLED???     
void OLED_Display_Off(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC????
	OLED_WR_Byte(0X10,OLED_CMD);  //DCDC OFF
	OLED_WR_Byte(0XAE,OLED_CMD);  //DISPLAY OFF
}		   			 
//????????,??????,?????????????!??????????!!!	  
void OLED_Clear(void)  
{  
	u8 i,n;		    
	for(i=0;i<8;i++)  
	{  
		OLED_WR_Byte (0xb0+i,OLED_CMD);    //??????????0~7??
		OLED_WR_Byte (0x00,OLED_CMD);      //???????¦Ë?¨¢??§Ö???
		OLED_WR_Byte (0x10,OLED_CMD);      //???????¦Ë?¨¢??§Ú???   
		for(n=0;n<128;n++)OLED_WR_Byte(0,OLED_DATA); 
	} //???????
}

void OLED_On(void)  
{  
	
}

//... (The rest of the high-level functions like OLED_ShowChar, OLED_ShowNum, etc. would go here)
//... They should work without modification as they call the abstracted OLED_WR_Byte.

//?????OLED
void OLED_Init(void)
{
	osDelay(100); // HAL_Delay is not ideal in FreeRTOS, use osDelay

	OLED_WR_Byte(0xAE,OLED_CMD);//--display off
	OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
	OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
	OLED_WR_Byte(0x40,OLED_CMD);//--set start line address
	OLED_WR_Byte(0xB0,OLED_CMD);//--set page address
	OLED_WR_Byte(0x81,OLED_CMD); // contract control
	OLED_WR_Byte(0xFF,OLED_CMD);//--128
	OLED_WR_Byte(0xA1,OLED_CMD);//set segment remap
	OLED_WR_Byte(0xA6,OLED_CMD);//--normal / reverse
	OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
	OLED_WR_Byte(0x3F,OLED_CMD);//--1/32 duty
	OLED_WR_Byte(0xC8,OLED_CMD);//Com scan direction
	OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset
	OLED_WR_Byte(0x00,OLED_CMD);//

	OLED_WR_Byte(0xD5,OLED_CMD);//set display clock divide ratio/oscillator frequency
	OLED_WR_Byte(0x80,OLED_CMD);//

	OLED_WR_Byte(0xD8,OLED_CMD);//set area color mode off
	OLED_WR_Byte(0x05,OLED_CMD);//

	OLED_WR_Byte(0xD9,OLED_CMD);//Set Pre-Charge Period
	OLED_WR_Byte(0xF1,OLED_CMD);//

	OLED_WR_Byte(0xDA,OLED_CMD);//set com pin configuartion
	OLED_WR_Byte(0x12,OLED_CMD);//

	OLED_WR_Byte(0xDB,OLED_CMD);//set Vcomh
	OLED_WR_Byte(0x30,OLED_CMD);//

	OLED_WR_Byte(0x8D,OLED_CMD);//set charge pump enable
	OLED_WR_Byte(0x14,OLED_CMD);//

	OLED_WR_Byte(0xAF,OLED_CMD);//--turn on oled panel
	OLED_Clear();
	OLED_Set_Pos(0,0);
}

//?????¦Ë???????????,???????????
//x:0~127
//y:0~63
//mode:0,???????;1,???????				 
//size:??????? 16/12 
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 Char_Size)
{      	
	unsigned char c=0,i=0;	
		c=chr-' ';//?????????			
		if(x>Max_Column-1){x=0;y=y+2;}
		if(Char_Size ==16)
			{
			OLED_Set_Pos(x,y);	
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i],OLED_DATA);
			OLED_Set_Pos(x,y+1);
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i+8],OLED_DATA);
			}
			else {	
				OLED_Set_Pos(x,y);
				for(i=0;i<6;i++)
				OLED_WR_Byte(F6x8[c][i],OLED_DATA);
				
			}
}
//m^n????
u32 oled_pow(u8 m,u8 n)
{
	u32 result=1;	 
	while(n--)result*=m;    
	return result;
}				  
//???2??????
//x,y :???????	 
//len :?????¦Ë??
//size:?????§³
//mode:??	0,?????;1,??????
//num:???(0~4294967295);	 		  
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size2)
{         	
	u8 t,temp;
	u8 enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/oled_pow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size2/2)*t,y,' ',size2);
				continue;
			}else enshow=1; 
		 	 
		}
	 	OLED_ShowChar(x+(size2/2)*t,y,temp+'0',size2); 
	}
} 
//????????????
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 Char_Size)
{
	unsigned char j=0;
	while (chr[j]!='\0')
	{		OLED_ShowChar(x,y,chr[j],Char_Size);
			x+=8;
		if(x>120){x=0;y+=2;}
			j++;
	}
}
//???????
void OLED_ShowCHinese(u8 x,u8 y,u8 no)
{      			    
	u8 t,adder=0;
	OLED_Set_Pos(x,y);	
    for(t=0;t<16;t++)
		{
				OLED_WR_Byte(Hzk[2*no][t],OLED_DATA);
				adder+=1;
     }	
		OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
			{	
				OLED_WR_Byte(Hzk[2*no+1][t],OLED_DATA);
				adder+=1;
      }					
}
/***********????????????????BMP??128??64?????????(x,y),x???¦¶0??127??y?????¦¶0??7*****************/
void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[])
{ 	
 unsigned int j=0;
 unsigned char x,y;
  
  if(y1%8==0) y=y1/8;      
  else y=y1/8+1;
	for(y=y0;y<y1;y++)
	{
		OLED_Set_Pos(x0,y);
    for(x=x0;x<x1;x++)
	    {      
	    	OLED_WR_Byte(BMP[j++],OLED_DATA);	    	
	    }
	}
}
