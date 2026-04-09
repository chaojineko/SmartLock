#ifndef __MENU_H
#define __MENU_H

#include "main.h"

// 定义UI状态
typedef enum {
    UI_STATE_WELCOME,
    UI_STATE_MAIN_MENU,
    UI_STATE_ADMIN_VERIFY,
    UI_STATE_PASSWORD_UNLOCK,
    UI_STATE_ADD_PASSWORD,
    UI_STATE_DEL_PASSWORD,
    UI_STATE_FINGERPRINT_UNLOCK,    // 指纹解锁状态
    UI_STATE_ADD_FINGERPRINT,       // 添加指纹状态
    UI_STATE_DEL_FINGERPRINT,       // 删除指纹状态
    UI_STATE_FINGERPRINT_SUCCESS,   // 指纹验证成功
    UI_STATE_FINGERPRINT_FAILED,     // 指纹验证失败
    UI_STATE_RFID_UNLOCK,           // RFID解锁状态
    UI_STATE_ADD_RFID,              // 添加RFID状态
    UI_STATE_DEL_RFID,              // 删除RFID状态
    UI_STATE_RFID_SUCCESS,          // RFID验证成功
    UI_STATE_RFID_FAILED,           // RFID验证失败
    UI_STATE_FACE_UNLOCK,           // 人脸解锁模式
    UI_STATE_QR_UNLOCK,             // 二维码解锁模式
    UI_STATE_BLE_UNLOCK,            // 蓝牙解锁模式
    UI_STATE_ADD_FACE,              // 人脸录入
    UI_STATE_DEL_FACE               // 人脸删除
} UI_State_t;

// UI任务
void Task_UI(void *argument);

#endif /* __MENU_H */
