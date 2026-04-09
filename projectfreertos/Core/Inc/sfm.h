#ifndef __SFM_H__
#define __SFM_H__

#include "main.h"

// 指纹模块状态定义
#define SFM_ACK_SUCCESS         0x00 //执行成功
#define SFM_ACK_FAIL            0x01 //执行失败
#define SFM_ACK_FULL            0x04 //数据库满
#define SFM_ACK_NOUSER          0x05 //没有这个用户
#define SFM_ACK_USER_EXIST      0x07 //用户已存在
#define SFM_ACK_TIMEOUT         0x08 //图像采集超时
#define SFM_ACK_HARDWAREERROR   0x0A //硬件错误
#define SFM_ACK_IMAGEERROR      0x10 //图像错误
#define SFM_ACK_BREAK           0x18 //终止当前指令
#define SFM_ACK_ALGORITHMFAIL   0x11 //贴膜攻击检测
#define SFM_ACK_HOMOLOGYFAIL    0x12 //同源性校验错误

// 指纹模块操作类型定义
typedef enum {
    SFM_NONE = 0,
    SFM_ADD,
    SFM_DEL,
    SFM_READ,
    SFM_DEL_ALL,
    SFM_VERIFY  // 新增：指纹验证
} SFM_Operation_t;

// 指纹模块状态定义
typedef enum {
    SFM_STATE_IDLE,
    SFM_STATE_WAITING_TOUCH,
    SFM_STATE_PROCESSING,
    SFM_STATE_SUCCESS,
    SFM_STATE_FAILED
} SFM_State_t;

// 指纹用户ID定义
#define SFM_MAX_USERS           100
#define SFM_DEFAULT_TIMEOUT     30000  // 30秒超时
#define SFM_UART_BAUD           57600

// 全局变量
extern volatile uint8_t  g_usart2_rx_buf[512];
extern volatile uint32_t g_usart2_rx_cnt;
extern volatile uint32_t g_usart2_rx_end;

// 函数声明
void usart2_init(uint32_t baud);
int32_t sfm_init(uint32_t baud);
uint8_t bcc_check(uint8_t *buf, uint32_t len);
int32_t sfm_ctrl_led(uint8_t led_start, uint8_t led_end, uint8_t period);
int32_t sfm_reg_user(uint16_t id);
int32_t sfm_compare_users(uint16_t *id);
int32_t sfm_get_user_total(uint16_t *user_total);
const char *sfm_error_code(uint8_t error_code);
uint32_t sfm_touch_sta(void);
void sfm_touch_init(void);
int32_t sfm_touch_check(void);
int32_t sfm_del_user(uint16_t id);
int32_t sfm_del_user_all(void);
int32_t sfm_get_unused_id(uint16_t *id);
int32_t sfm_wait_touch(uint32_t timeout_ms);

// 新增函数声明
int32_t sfm_verify_fingerprint(uint16_t *user_id, uint32_t timeout);
SFM_State_t sfm_get_state(void);
void sfm_reset_state(void);

#endif /* __SFM_H__ */
