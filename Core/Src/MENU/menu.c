#include "menu.h"
#include "oled.h"
#include "keyboard.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tim.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "uart_debug.h"
#include "keyboard.h"
#include "rtc.h"
#include "servo.h"
#include "sfm.h"
#include "rc522.h"
#include "usart.h"
#include "../FLASH/flash.h"

// 当前UI状态，默认为欢迎界面
static UI_State_t current_ui_state = UI_STATE_WELCOME;
static int8_t menu_cursor = 0; // 主菜单光标
static uint8_t main_menu_force_redraw = 1;

// 函数声明
static void display_welcome(void);
static void display_main_menu(void);
static void display_admin_verify(void);
static void display_password_unlock(void);
static void display_add_password(void);
static void display_del_password(void);
static void display_fingerprint_unlock(void);
static void display_add_fingerprint(void);
static void display_del_fingerprint(void);
static void display_fingerprint_result(void);
static void display_rfid_unlock(void);
static void display_add_rfid(void);
static void display_del_rfid(void);
static void display_rfid_result(void);
static void display_face_unlock(void);
static void display_qr_unlock(void);
static void display_ble_unlock(void);
static void display_add_face(void);
static void display_del_face(void);
static void menu_esp32_send_cmd(const char *cmd);
static void menu_esp32_set_mode(const char *mode_cmd);
static uint8_t menu_cursor_requires_admin(int8_t cursor);
static int32_t menu_sfm_ensure_ready(void);
static void menu_load_lock_data(void);
static void menu_save_lock_data(void);

#define MENU_ITEM_COUNT 14
#define MENU_MODE_TIMEOUT_MS 15000U

// 密码输入相关
static char password_buffer[7] = {0};
static uint8_t password_index = 0;
static char saved_password[7] = "123456";
static uint8_t saved_password_len = 6;
static UI_State_t admin_next_state = UI_STATE_MAIN_MENU;

// RFID验证相关

// 指纹验证相关
static uint16_t fingerprint_user_id = 0;
static uint8_t sfm_ready = 0;
static char del_fp_id_buf[4] = {0};
static uint8_t del_fp_id_len = 0;

static void menu_load_lock_data(void)
{
    LockPersistData_t persist;

    if (FlashStorage_Init() == 0U)
    {
        printf("[LOCK DATA] SPI flash not detected, use default data.\r\n");
        return;
    }

    if (FlashStorage_Load(&persist) == 0U)
    {
        printf("[LOCK DATA] No valid record, use default data.\r\n");
        return;
    }

    if ((persist.password_len >= 4U) && (persist.password_len <= 6U))
    {
        memset(saved_password, 0, sizeof(saved_password));
        memcpy(saved_password, persist.password, persist.password_len);
        saved_password[persist.password_len] = '\0';
        saved_password_len = persist.password_len;
    }

    RFID_ImportStoredCards((const uint8_t *)persist.cards, persist.card_count);
    printf("[LOCK DATA] Loaded: pwd_len=%u, cards=%u\r\n", saved_password_len, persist.card_count);
}

static void menu_save_lock_data(void)
{
    LockPersistData_t persist;
    uint8_t card_count = 0U;

    memset(&persist, 0, sizeof(persist));

    persist.password_len = saved_password_len;
    memcpy(persist.password, saved_password, sizeof(saved_password));

    RFID_ExportStoredCards((uint8_t *)persist.cards, LOCK_MAX_RFID_CARDS, &card_count);
    persist.card_count = card_count;

    if (FlashStorage_Save(&persist) == 0U)
    {
        printf("[LOCK DATA] Save failed.\r\n");
    }
    else
    {
        printf("[LOCK DATA] Saved: pwd_len=%u, cards=%u\r\n", persist.password_len, persist.card_count);
    }
}

static int32_t menu_sfm_ensure_ready(void)
{
    int32_t sfm_rt;

    if (sfm_ready != 0U)
    {
        return SFM_ACK_SUCCESS;
    }

    sfm_rt = sfm_init(SFM_UART_BAUD);
    if (sfm_rt == SFM_ACK_SUCCESS)
    {
        sfm_ready = 1;
    }

    return sfm_rt;
}

static void menu_esp32_send_cmd(const char *cmd)
{
    char tx[32] = {0};
    int len;

    if (cmd == NULL)
    {
        return;
    }

    len = snprintf(tx, sizeof(tx), "%s\r\n", cmd);
    if ((len <= 0) || (len > (int)sizeof(tx)))
    {
        return;
    }

    (void)HAL_UART_Transmit(&huart3, (uint8_t *)tx, (uint16_t)len, 120);
    printf("[MENU->ESP32] %s\r\n", cmd);
}

static void menu_esp32_set_mode(const char *mode_cmd)
{
    menu_esp32_send_cmd(mode_cmd);
    // Let ESP32 mode task consume the command before UI loop proceeds.
    vTaskDelay(pdMS_TO_TICKS(40));
}

static uint8_t menu_cursor_requires_admin(int8_t cursor)
{
    return (cursor >= 6) ? 1U : 0U;
}

void Task_UI(void *argument)
{
    static UI_State_t prev_ui_state = UI_STATE_WELCOME;
    int32_t sfm_rt;

    OLED_Init();
    Servo_Init();
    RFID_Init();
    menu_load_lock_data();
    sfm_rt = sfm_init(SFM_UART_BAUD);
    if (sfm_rt != SFM_ACK_SUCCESS)
    {
        printf("SFM init failed: %s\r\n", sfm_error_code((uint8_t)sfm_rt));
        sfm_ready = 0;
    }
    else
    {
        sfm_ready = 1;
    }
    
    OLED_Clear();

    while(1)
    {
        if ((current_ui_state == UI_STATE_MAIN_MENU) && (prev_ui_state != UI_STATE_MAIN_MENU))
        {
            main_menu_force_redraw = 1;
        }

        prev_ui_state = current_ui_state;

        switch(current_ui_state)
        {
            case UI_STATE_WELCOME:
                display_welcome();
                break;
            case UI_STATE_MAIN_MENU:
                display_main_menu();
                break;
            case UI_STATE_ADMIN_VERIFY:
                display_admin_verify();
                break;
            case UI_STATE_PASSWORD_UNLOCK:
                display_password_unlock();
                break;
            case UI_STATE_FINGERPRINT_UNLOCK:
                display_fingerprint_unlock();
                break;
            case UI_STATE_ADD_FINGERPRINT:
                display_add_fingerprint();
                break;
            case UI_STATE_DEL_FINGERPRINT:
                display_del_fingerprint();
                break;
            case UI_STATE_FINGERPRINT_SUCCESS:
            case UI_STATE_FINGERPRINT_FAILED:
                display_fingerprint_result();
                break;
            case UI_STATE_ADD_PASSWORD:
                display_add_password();
                break;
            case UI_STATE_DEL_PASSWORD:
                display_del_password();
                break;
            case UI_STATE_RFID_UNLOCK:
                display_rfid_unlock();
                break;
            case UI_STATE_ADD_RFID:
                display_add_rfid();
                break;
            case UI_STATE_DEL_RFID:
                display_del_rfid();
                break;
            case UI_STATE_RFID_SUCCESS:
            case UI_STATE_RFID_FAILED:
                display_rfid_result();
                break;
            case UI_STATE_FACE_UNLOCK:
                display_face_unlock();
                break;
            case UI_STATE_QR_UNLOCK:
                display_qr_unlock();
                break;
            case UI_STATE_BLE_UNLOCK:
                display_ble_unlock();
                break;
            case UI_STATE_ADD_FACE:
                display_add_face();
                break;
            case UI_STATE_DEL_FACE:
                display_del_face();
                break;
            default:
                current_ui_state = UI_STATE_WELCOME;
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms刷新一次
    }
}

static void display_welcome(void)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    char time_str[17];
    char date_str[17];

    // 首次进入先显示启动页
    static uint8_t first_entry = 1;
    static uint8_t last_minute = 0xFF;
    static uint8_t last_hour = 0xFF;
    static uint8_t last_date = 0xFF;
    static uint8_t last_month = 0xFF;
    static uint8_t last_year = 0xFF;
    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Smart Lock Boot", 8);
        OLED_ShowString(0, 2, (uint8_t *)"RFID/FP/PWD Ready", 8);
        OLED_ShowString(0, 4, (uint8_t *)"Version: 1.0", 8);
        OLED_ShowString(0, 7, (uint8_t *)"Loading...", 8);
        vTaskDelay(pdMS_TO_TICKS(1200));
        first_entry = 0;
        last_minute = 0xFF;
        last_hour = 0xFF;
        last_date = 0xFF;
        last_month = 0xFF;
        last_year = 0xFF;
        OLED_Clear();
    }

    // 获取并显示时间，按分钟/日期变化刷新
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    if ((last_minute != sTime.Minutes) ||
        (last_hour != sTime.Hours) ||
        (last_date != sDate.Date) ||
        (last_month != sDate.Month) ||
        (last_year != sDate.Year))
    {
        snprintf(date_str, sizeof(date_str), "Date 20%02d-%02d-%02d", sDate.Year, sDate.Month, sDate.Date);
        snprintf(time_str, sizeof(time_str), "Time %02d:%02d", sTime.Hours, sTime.Minutes);

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Smart Lock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)date_str, 8);
        OLED_ShowString(0, 4, (uint8_t *)time_str, 8);
        OLED_ShowString(0, 7, (uint8_t *)"Press any key...", 8);

        last_minute = sTime.Minutes;
        last_hour = sTime.Hours;
        last_date = sDate.Date;
        last_month = sDate.Month;
        last_year = sDate.Year;
    }

    // 按任意键进入主菜单
    if (Keypad_Scan() != KEYPAD_NO_KEY)
    {
        current_ui_state = UI_STATE_MAIN_MENU;
        menu_cursor = 0; // 重置光标
        OLED_Clear();
    }
}


static void display_main_menu(void)
{
    static int8_t last_cursor = -1;
    static uint8_t last_page = 0xFF;
    static uint8_t last_card_count = 0xFF;

    static const char *menu_items[MENU_ITEM_COUNT] = {
        "Pwd Unlock",
        "Face Unlock",
        "QR Unlock",
        "BLE Unlock",
        "Fp Unlock",
        "RFID Unlock",
        "Add Pwd",
        "Del Pwd",
        "Add Face",
        "Del Face",
        "Add Fp",
        "Add RFID",
        "Del RFID",
        "Del Fp"
    };

    uint8_t card_count = RFID_GetStoredCardCount();
    uint8_t page_count = (uint8_t)((MENU_ITEM_COUNT + 3U) / 4U);
    uint8_t page = (uint8_t)(menu_cursor / 4);
    uint8_t page_start = page * 4;
    uint8_t page_end = (uint8_t)((page_start + 4) > MENU_ITEM_COUNT ? MENU_ITEM_COUNT : (page_start + 4));

    // 仅在菜单状态变化时重绘，避免OLED残影与闪烁
    if (main_menu_force_redraw || (last_cursor != menu_cursor) || (last_page != page) || (last_card_count != card_count))
    {
        char info_line[17] = {0};
        char row_text[17] = {0};

        OLED_Clear();

        // 第0行：标题 + 卡片数 + 页码
        OLED_ShowString(0, 0, (uint8_t *)"Smart Lock", 8);
        snprintf(info_line, sizeof(info_line), "C:%02d P:%d/%d", card_count, page + 1, page_count);
        OLED_ShowString(56, 0, (uint8_t *)info_line, 8);

        // 第1行：分隔线
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);

        // 第2~5行：菜单项（每页4项）
        for (uint8_t i = page_start; i < page_end; i++)
        {
            uint8_t y = (uint8_t)(2 + (i - page_start));
            snprintf(row_text, sizeof(row_text), "%c%u %-12s",
                     (i == (uint8_t)menu_cursor) ? '>' : ' ',
                     (unsigned)(i + 1),
                     menu_items[i]);
            OLED_ShowString(0, y, (uint8_t *)row_text, 8);
        }

        // 第6行：空白保留，视觉上更清爽
        OLED_ShowString(0, 6, (uint8_t *)"                ", 8);

        // 第7行：按键提示
        OLED_ShowString(0, 7, (uint8_t *)"2^ 8v #OK *Back", 8);

        last_cursor = menu_cursor;
        last_page = page;
        last_card_count = card_count;
        main_menu_force_redraw = 0;
    }

    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        // 等待按键释放，增加超时
        uint32_t release_start = xTaskGetTickCount();
        while(Keypad_Scan() != KEYPAD_NO_KEY) {
            if (xTaskGetTickCount() - release_start > pdMS_TO_TICKS(500)) { // 500ms超时
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        char key_char = Keypad_GetChar(key);
        // 使用 '2' 向上, '8' 向下, '#' 确认, '*' 返回
        if (key_char == '8') // Down
        {
            menu_cursor = (menu_cursor + 1) % MENU_ITEM_COUNT;
        }
        else if (key_char == '2') // Up
        {
            menu_cursor = (menu_cursor - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        }
        else if (key_char == '#') // Confirm
        {
            UI_State_t selected_state = UI_STATE_MAIN_MENU;

            if (menu_cursor == 0)
            {
                selected_state = UI_STATE_PASSWORD_UNLOCK;
            }
            else if (menu_cursor == 1)
            {
                selected_state = UI_STATE_FACE_UNLOCK;
            }
            else if (menu_cursor == 2)
            {
                selected_state = UI_STATE_QR_UNLOCK;
            }
            else if (menu_cursor == 3)
            {
                selected_state = UI_STATE_BLE_UNLOCK;
            }
            else if (menu_cursor == 4)
            {
                selected_state = UI_STATE_FINGERPRINT_UNLOCK;
            }
            else if (menu_cursor == 5)
            {
                selected_state = UI_STATE_RFID_UNLOCK;
            }
            else if (menu_cursor == 6)
            {
                selected_state = UI_STATE_ADD_PASSWORD;
            }
            else if (menu_cursor == 7)
            {
                selected_state = UI_STATE_DEL_PASSWORD;
            }
            else if (menu_cursor == 8)
            {
                selected_state = UI_STATE_ADD_FACE;
            }
            else if (menu_cursor == 9)
            {
                selected_state = UI_STATE_DEL_FACE;
            }
            else if (menu_cursor == 10)
            {
                selected_state = UI_STATE_ADD_FINGERPRINT;
            }
            else if (menu_cursor == 11)
            {
                selected_state = UI_STATE_ADD_RFID;
            }
            else if (menu_cursor == 12)
            {
                selected_state = UI_STATE_DEL_RFID;
            }
            else if (menu_cursor == 13)
            {
                selected_state = UI_STATE_DEL_FINGERPRINT;
            }

            if (menu_cursor_requires_admin(menu_cursor) != 0U)
            {
                admin_next_state = selected_state;
                current_ui_state = UI_STATE_ADMIN_VERIFY;
            }
            else
            {
                current_ui_state = selected_state;
            }

            memset(password_buffer, 0, sizeof(password_buffer));
            password_index = 0;
            OLED_Clear();
        }
        else if (key_char == '*') // Back
        {
            current_ui_state = UI_STATE_WELCOME;
            OLED_Clear();
        }
    }
}

static void display_admin_verify(void)
{
    static uint8_t first_entry = 1;
    static uint8_t last_len = 0xFF;
    static char verify_buf[7] = {0};
    static uint8_t verify_len = 0;

    if (first_entry)
    {
        memset(verify_buf, 0, sizeof(verify_buf));
        verify_len = 0;
        last_len = 0xFF;
        OLED_Clear();
        first_entry = 0;
    }

    if (last_len != verify_len)
    {
        char masked[7] = {0};
        for (uint8_t i = 0; i < verify_len && i < 6; i++)
        {
            masked[i] = '*';
        }

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Admin Verify", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Pwd:", 8);
        OLED_ShowString(32, 3, (uint8_t *)masked, 8);
        OLED_ShowString(0, 5, (uint8_t *)"#=OK *=Cancel", 8);
        last_len = verify_len;
    }

    {
        int key = Keypad_Scan();
        if (key != KEYPAD_NO_KEY)
        {
            while (Keypad_Scan() != KEYPAD_NO_KEY)
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            {
                char key_char = Keypad_GetChar(key);

                if ((key_char >= '0') && (key_char <= '9') && (verify_len < 6))
                {
                    verify_buf[verify_len++] = key_char;
                    verify_buf[verify_len] = '\0';
                }
                else if (key_char == '#')
                {
                    if ((verify_len == saved_password_len) && (strcmp(verify_buf, saved_password) == 0))
                    {
                        current_ui_state = admin_next_state;
                    }
                    else
                    {
                        OLED_Clear();
                        OLED_ShowString(0, 3, (uint8_t *)"Auth Failed", 8);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        current_ui_state = UI_STATE_MAIN_MENU;
                    }

                    memset(verify_buf, 0, sizeof(verify_buf));
                    verify_len = 0;
                    first_entry = 1;
                    OLED_Clear();
                }
                else if (key_char == '*')
                {
                    current_ui_state = UI_STATE_MAIN_MENU;
                    memset(verify_buf, 0, sizeof(verify_buf));
                    verify_len = 0;
                    first_entry = 1;
                    OLED_Clear();
                }
            }
        }
    }
}



static void display_fingerprint_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint32_t verify_start_time = 0;
    static int32_t last_left_sec = -1;

    if (first_entry)
    {
        int32_t sfm_rt = menu_sfm_ensure_ready();
        if (sfm_rt != SFM_ACK_SUCCESS)
        {
            OLED_Clear();
            OLED_ShowString(0, 2, (uint8_t *)"SFM Not Ready", 8);
            OLED_ShowString(0, 4, (uint8_t *)"Check power/UART", 8);
            vTaskDelay(pdMS_TO_TICKS(2000));
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            return;
        }

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Fingerprint Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Touch sensor...", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Cancel", 8);
        (void)sfm_ctrl_led(0x01, 0x01, 0x05);
        verify_start_time = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    int32_t left_sec = 30 - (int32_t)((xTaskGetTickCount() - verify_start_time) / pdMS_TO_TICKS(1000));
    if (left_sec < 0) left_sec = 0;
    if (left_sec != last_left_sec)
    {
        char line[17] = {0};
        snprintf(line, sizeof(line), "Timeout: %2lds", (long)left_sec);
        OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
        OLED_ShowString(0, 5, (uint8_t *)line, 8);
        last_left_sec = left_sec;
    }

    // 适当延长触摸等待窗口，降低漏检率。
    int32_t result = sfm_verify_fingerprint(&fingerprint_user_id, 1200);

    if (result == SFM_ACK_SUCCESS)
    {
        current_ui_state = UI_STATE_FINGERPRINT_SUCCESS;
        first_entry = 1;
    }
    else if ((result == SFM_ACK_HARDWAREERROR) || (result == SFM_ACK_FULL))
    {
        OLED_Clear();
        OLED_ShowString(0, 2, (uint8_t *)"Fingerprint Error", 8);
        OLED_ShowString(0, 4, (uint8_t *)"Sensor not ready", 8);
        vTaskDelay(pdMS_TO_TICKS(2000));
        current_ui_state = UI_STATE_MAIN_MENU;
        first_entry = 1;
    }

    // 检查超时
    if (xTaskGetTickCount() - verify_start_time > pdMS_TO_TICKS(30000)) // 30秒超时
    {
        current_ui_state = UI_STATE_FINGERPRINT_FAILED;
        first_entry = 1;
    }

    // 检查返回键
    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        char key_char = Keypad_GetChar(key);
        if (key_char == '*')
        {
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
        }
    }
}
static void display_add_fingerprint(void)
{
    static uint8_t first_entry = 1;
    uint16_t user_id;
    
    if (first_entry)
    {
        int32_t sfm_rt = menu_sfm_ensure_ready();
        if (sfm_rt != SFM_ACK_SUCCESS)
        {
            OLED_Clear();
            OLED_ShowString(0, 2, (uint8_t *)"SFM Not Ready", 8);
            OLED_ShowString(0, 4, (uint8_t *)"Check power/UART", 8);
            vTaskDelay(pdMS_TO_TICKS(2000));
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            return;
        }

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Add Fingerprint", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Getting new ID...", 8);
        OLED_ShowString(0, 7, (uint8_t *)"Please wait", 8);
        vTaskDelay(pdMS_TO_TICKS(100));

        first_entry = 0;
    }
    
    // 获取未使用的用户ID
    int32_t result = sfm_get_unused_id(&user_id);
    
    if (result == SFM_ACK_SUCCESS)
    {
        char id_str[17] = {0};
        snprintf(id_str, sizeof(id_str), "Enroll ID:%u", user_id);
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Add Fingerprint", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t*)id_str, 8);
        OLED_ShowString(0, 4, (uint8_t *)"Touch sensor...", 8);
        OLED_ShowString(0, 7, (uint8_t *)"Do not move", 8);
        (void)sfm_ctrl_led(0x01, 0x01, 0x05);

        // 直接进入模块录入流程，避免触摸检测误判导致无法开始。
        vTaskDelay(pdMS_TO_TICKS(120));
        
        // 注册用户指纹
        result = sfm_reg_user(user_id);
        
        if (result == SFM_ACK_SUCCESS)
        {
            OLED_Clear();
            OLED_ShowString(0, 2, (uint8_t *)"Fingerprint Added", 8);
            OLED_ShowString(0, 4, (uint8_t*)id_str, 8);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        else
        {
            OLED_Clear();
            OLED_ShowString(0, 2, (uint8_t *)"Add Fingerprint NG", 8);
            OLED_ShowString(0, 4, (uint8_t*)sfm_error_code(result), 8);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
    else
    {
        OLED_Clear();
        OLED_ShowString(0, 3, (uint8_t *)"Database Full!", 8);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    current_ui_state = UI_STATE_MAIN_MENU;
    first_entry = 1;
    //OLED_Clear(); // 由下一个状态的display函数负责清屏
}
static void display_fingerprint_result(void)
{
    if (current_ui_state == UI_STATE_FINGERPRINT_SUCCESS)
    {
        char user_str[17] = {0};
        snprintf(user_str, sizeof(user_str), "User ID:%u", fingerprint_user_id);
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Fingerprint Result", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Verified", 8);
        OLED_ShowString(0, 4, (uint8_t*)user_str, 8);
        OLED_ShowString(0, 7, (uint8_t *)"Door Unlocking...", 8);
        
        // 执行开锁操作
        Servo_Unlock();
        vTaskDelay(pdMS_TO_TICKS(2000));
        Servo_Lock();
    }
    else if (current_ui_state == UI_STATE_FINGERPRINT_FAILED)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Fingerprint Result", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Failed", 8);
        OLED_ShowString(0, 7, (uint8_t *)"Returning menu...", 8);
    }
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    current_ui_state = UI_STATE_MAIN_MENU;
    OLED_Clear();
}

static void display_password_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint8_t last_len = 0xFF;
    
    if (first_entry)
    {
        OLED_Clear();
        last_len = 0xFF;
        first_entry = 0;
    }

    if (last_len != password_index)
    {
        char masked[7] = {0};
        for (uint8_t i = 0; i < password_index && i < 6; i++)
        {
            masked[i] = '*';
        }

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Password Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Input:", 8);
        OLED_ShowString(40, 3, (uint8_t *)masked, 8);
        OLED_ShowString(0, 5, (uint8_t *)"Len:", 8);
        OLED_ShowNum(24, 5, password_index, 1, 8);
        OLED_ShowString(0, 7, (uint8_t *)"#=OK *=Back", 8);

        last_len = password_index;
    }

    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        // 等待按键释放
        while(Keypad_Scan() != KEYPAD_NO_KEY) vTaskDelay(pdMS_TO_TICKS(20));

        char key_char = Keypad_GetChar(key);

        // 如果是数字键并且密码没满
        if ((key_char >= '0' && key_char <= '9') && (password_index < 6))
        {
            password_buffer[password_index++] = key_char;
            password_buffer[password_index] = '\0';
        }
        else if (key_char == '#') // 确认键
        {
            if ((password_index == saved_password_len) && (strcmp(password_buffer, saved_password) == 0))
            {
                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Password OK", 8);
                OLED_ShowString(0, 4, (uint8_t *)"Door Unlocked", 8);
                Servo_Unlock(); // 开锁
                vTaskDelay(pdMS_TO_TICKS(4000)); // 保持开锁状态4秒
                Servo_Lock();   // 关锁
                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Door Locked", 8);
            }
            else
            {
                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Wrong Password", 8);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            current_ui_state = UI_STATE_MAIN_MENU;
            // 清理密码缓冲区
            memset(password_buffer, 0, sizeof(password_buffer));
            password_index = 0;
            first_entry = 1;
            OLED_Clear();
        }
        else if (key_char == '*') // 取消/返回键
        {
            current_ui_state = UI_STATE_MAIN_MENU;
            // 清理密码缓冲区
            memset(password_buffer, 0, sizeof(password_buffer));
            password_index = 0;
            first_entry = 1;
            OLED_Clear();
        }
    }
}

static void display_add_password(void)
{
    static uint8_t first_entry = 1;
    static uint8_t last_len = 0xFF;
    static uint8_t add_step = 0; // 0: input new, 1: confirm
    static char first_password[7] = {0};
    
    if (first_entry)
    {
        memset(password_buffer, 0, sizeof(password_buffer));
        password_index = 0;
        memset(first_password, 0, sizeof(first_password));
        add_step = 0;
        OLED_Clear();
        last_len = 0xFF;
        first_entry = 0;
    }

    if (last_len != password_index)
    {
        char masked[7] = {0};
        for (uint8_t i = 0; i < password_index && i < 6; i++)
        {
            masked[i] = '*';
        }

        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Add Password", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 2, (uint8_t *)((add_step == 0U) ? "Step1: New" : "Step2: Confirm"), 8);
        OLED_ShowString(0, 3, (uint8_t *)"Input:", 8);
        OLED_ShowString(32, 3, (uint8_t *)masked, 8);
        OLED_ShowString(0, 5, (uint8_t *)"Len:", 8);
        OLED_ShowNum(24, 5, password_index, 1, 8);
        OLED_ShowString(0, 6, (uint8_t *)"Need 4~6 digits", 8);
        OLED_ShowString(0, 7, (uint8_t *)"#=Next/Save *=Back", 8);

        last_len = password_index;
    }

    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        // 等待按键释放
        while(Keypad_Scan() != KEYPAD_NO_KEY) vTaskDelay(pdMS_TO_TICKS(20));
        
        char key_char = Keypad_GetChar(key);

        // 如果是数字键并且密码没满
        if ((key_char >= '0' && key_char <= '9') && (password_index < 6))
        {
            password_buffer[password_index++] = key_char;
            password_buffer[password_index] = '\0';
        }
        else if (key_char == '#') // 确认键
        {
            if (password_index < 4U)
            {
                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Too Short", 8);
                OLED_ShowString(0, 5, (uint8_t *)"Use 4~6 digits", 8);
                vTaskDelay(pdMS_TO_TICKS(1200));
                last_len = 0xFF;
                return;
            }

            if (add_step == 0U)
            {
                memcpy(first_password, password_buffer, sizeof(first_password));
                memset(password_buffer, 0, sizeof(password_buffer));
                password_index = 0;
                add_step = 1;
                last_len = 0xFF;
            }
            else
            {
                OLED_Clear();
                if (strcmp(first_password, password_buffer) == 0)
                {
                    memcpy(saved_password, first_password, sizeof(saved_password));
                    saved_password_len = (uint8_t)strlen(saved_password);
                    menu_save_lock_data();
                    OLED_ShowString(0, 3, (uint8_t *)"Password Saved", 8);
                }
                else
                {
                    OLED_ShowString(0, 3, (uint8_t *)"Mismatch", 8);
                    OLED_ShowString(0, 5, (uint8_t *)"Not Updated", 8);
                }

                vTaskDelay(pdMS_TO_TICKS(1500));
                current_ui_state = UI_STATE_MAIN_MENU;
                memset(password_buffer, 0, sizeof(password_buffer));
                password_index = 0;
                memset(first_password, 0, sizeof(first_password));
                add_step = 0;
                OLED_Clear();
                first_entry = 1;
            }
        }
        else if (key_char == '*') // 取消/返回键
        {
            if (add_step == 1U)
            {
                memset(password_buffer, 0, sizeof(password_buffer));
                password_index = 0;
                add_step = 0;
                last_len = 0xFF;
            }
            else
            {
                current_ui_state = UI_STATE_MAIN_MENU;
                memset(password_buffer, 0, sizeof(password_buffer));
                password_index = 0;
                memset(first_password, 0, sizeof(first_password));
                add_step = 0;
                OLED_Clear();
                first_entry = 1;
            }
        }
    }
}

static void display_del_password(void)
{
    static uint8_t first_entry = 1;

    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Delete Password", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"#=Reset Default", 8);
        OLED_ShowString(0, 5, (uint8_t *)"*=Back", 8);
        first_entry = 0;
    }

    {
        int key = Keypad_Scan();
        if (key != KEYPAD_NO_KEY)
        {
            while (Keypad_Scan() != KEYPAD_NO_KEY) { vTaskDelay(pdMS_TO_TICKS(20)); }

            if (Keypad_GetChar(key) == '#')
            {
                memcpy(saved_password, "123456", sizeof(saved_password));
                saved_password_len = 6;
                menu_save_lock_data();

                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Password Reset", 8);
                OLED_ShowString(0, 5, (uint8_t *)"Default:123456", 8);
                vTaskDelay(pdMS_TO_TICKS(1500));
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
            else if (Keypad_GetChar(key) == '*')
            {
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}

static void display_face_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint32_t start_tick = 0;
    static int32_t last_left_sec = -1;

    if (first_entry)
    {
        menu_esp32_set_mode("MODE_FACE");
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Face Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Look at camera", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Stop", 8);
        start_tick = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    {
        int32_t left_sec = (int32_t)(MENU_MODE_TIMEOUT_MS / 1000U) -
                           (int32_t)((xTaskGetTickCount() - start_tick) / pdMS_TO_TICKS(1000));
        if (left_sec < 0)
        {
            left_sec = 0;
        }

        if (left_sec != last_left_sec)
        {
            char line[17] = {0};
            snprintf(line, sizeof(line), "Timeout:%2lds", (long)left_sec);
            OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
            OLED_ShowString(0, 5, (uint8_t *)line, 8);
            last_left_sec = left_sec;
        }

        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(MENU_MODE_TIMEOUT_MS))
        {
            menu_esp32_set_mode("MODE_OFF");
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
            return;
        }

        {
            int key = Keypad_Scan();
            if ((key != KEYPAD_NO_KEY) && (Keypad_GetChar(key) == '*'))
            {
                menu_esp32_set_mode("MODE_OFF");
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}

static void display_qr_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint32_t start_tick = 0;
    static int32_t last_left_sec = -1;

    if (first_entry)
    {
        menu_esp32_set_mode("MODE_QR");
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"QR Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Show QR to cam", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Stop", 8);
        start_tick = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    {
        int32_t left_sec = (int32_t)(MENU_MODE_TIMEOUT_MS / 1000U) -
                           (int32_t)((xTaskGetTickCount() - start_tick) / pdMS_TO_TICKS(1000));
        if (left_sec < 0)
        {
            left_sec = 0;
        }

        if (left_sec != last_left_sec)
        {
            char line[17] = {0};
            snprintf(line, sizeof(line), "Timeout:%2lds", (long)left_sec);
            OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
            OLED_ShowString(0, 5, (uint8_t *)line, 8);
            last_left_sec = left_sec;
        }

        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(MENU_MODE_TIMEOUT_MS))
        {
            menu_esp32_set_mode("MODE_OFF");
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
            return;
        }

        {
            int key = Keypad_Scan();
            if ((key != KEYPAD_NO_KEY) && (Keypad_GetChar(key) == '*'))
            {
                menu_esp32_set_mode("MODE_OFF");
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}

static void display_ble_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint32_t start_tick = 0;
    static int32_t last_left_sec = -1;

    if (first_entry)
    {
        menu_esp32_set_mode("MODE_BLE");
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"BLE Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Use mini program", 8);
        OLED_ShowString(0, 4, (uint8_t *)"Tap BLE unlock", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Stop", 8);
        start_tick = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    {
        int32_t left_sec = (int32_t)(MENU_MODE_TIMEOUT_MS / 1000U) -
                           (int32_t)((xTaskGetTickCount() - start_tick) / pdMS_TO_TICKS(1000));
        if (left_sec < 0)
        {
            left_sec = 0;
        }

        if (left_sec != last_left_sec)
        {
            char line[17] = {0};
            snprintf(line, sizeof(line), "Timeout:%2lds", (long)left_sec);
            OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
            OLED_ShowString(0, 5, (uint8_t *)line, 8);
            last_left_sec = left_sec;
        }

        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(MENU_MODE_TIMEOUT_MS))
        {
            menu_esp32_set_mode("MODE_OFF");
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
            return;
        }

        {
            int key = Keypad_Scan();
            if ((key != KEYPAD_NO_KEY) && (Keypad_GetChar(key) == '*'))
            {
                menu_esp32_set_mode("MODE_OFF");
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}

static void display_add_face(void)
{
    static uint8_t first_entry = 1;
    static uint32_t start_tick = 0;

    if (first_entry)
    {
        menu_esp32_set_mode("FACE_ADD");
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Add Face", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Look at camera", 8);
        OLED_ShowString(0, 5, (uint8_t *)"Wait auto save", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Cancel", 8);
        start_tick = xTaskGetTickCount();
        first_entry = 0;
    }

    if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(10000))
    {
        menu_esp32_set_mode("MODE_OFF");
        current_ui_state = UI_STATE_MAIN_MENU;
        first_entry = 1;
        OLED_Clear();
        return;
    }

    {
        int key = Keypad_Scan();
        if ((key != KEYPAD_NO_KEY) && (Keypad_GetChar(key) == '*'))
        {
            menu_esp32_set_mode("MODE_OFF");
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
        }
    }
}

static void display_del_face(void)
{
    static uint8_t first_entry = 1;

    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Delete Face", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"#=Delete All", 8);
        OLED_ShowString(0, 5, (uint8_t *)"*=Back", 8);
        first_entry = 0;
    }

    {
        int key = Keypad_Scan();
        if (key != KEYPAD_NO_KEY)
        {
            while (Keypad_Scan() != KEYPAD_NO_KEY) { vTaskDelay(pdMS_TO_TICKS(20)); }

            if (Keypad_GetChar(key) == '#')
            {
                menu_esp32_set_mode("FACE_DEL");
                menu_esp32_set_mode("MODE_OFF");
                OLED_Clear();
                OLED_ShowString(0, 3, (uint8_t *)"Face Data Cleared", 8);
                vTaskDelay(pdMS_TO_TICKS(1200));
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
            else if (Keypad_GetChar(key) == '*')
            {
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}

static void display_del_rfid(void)
{
    static uint8_t first_entry = 1;
    static uint32_t del_start_time = 0;
    static int32_t last_left_sec = -1;
    uint8_t card_id[4];

    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Delete RFID", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Scan to delete", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Back", 8);
        del_start_time = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    {
        int32_t left_sec = 8 - (int32_t)((xTaskGetTickCount() - del_start_time) / pdMS_TO_TICKS(1000));
        if (left_sec < 0) left_sec = 0;
        if (left_sec != last_left_sec)
        {
            char line[17] = {0};
            snprintf(line, sizeof(line), "Timeout: %lds", (long)left_sec);
            OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
            OLED_ShowString(0, 5, (uint8_t *)line, 8);
            last_left_sec = left_sec;
        }
    }

    if (xTaskGetTickCount() - del_start_time < pdMS_TO_TICKS(8000))
    {
        if (RFID_VerifyCard(card_id))
        {
            OLED_Clear();
            if (RFID_RemoveStoredCard(card_id))
            {
                menu_save_lock_data();
                OLED_ShowString(0, 3, (uint8_t *)"RFID Deleted", 8);
            }
            else
            {
                OLED_ShowString(0, 3, (uint8_t *)"Card Not Found", 8);
            }
            vTaskDelay(pdMS_TO_TICKS(1200));
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
            return;
        }
    }
    else
    {
        current_ui_state = UI_STATE_MAIN_MENU;
        first_entry = 1;
        OLED_Clear();
        return;
    }

    {
        int key = Keypad_Scan();
        if ((key != KEYPAD_NO_KEY) && (Keypad_GetChar(key) == '*'))
        {
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
        }
    }
}

static void display_rfid_unlock(void)
{
    static uint8_t first_entry = 1;
    static uint32_t verify_start_time = 0;
    static int32_t last_left_sec = -1;
    uint8_t card_id[4];
 
    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"RFID Unlock", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Scan card...", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Cancel", 8);
        verify_start_time = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    int32_t left_sec = 5 - (int32_t)((xTaskGetTickCount() - verify_start_time) / pdMS_TO_TICKS(1000));
    if (left_sec < 0) left_sec = 0;
    if (left_sec != last_left_sec)
    {
        char line[17] = {0};
        snprintf(line, sizeof(line), "Timeout: %lds", (long)left_sec);
        OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
        OLED_ShowString(0, 5, (uint8_t *)line, 8);
        last_left_sec = left_sec;
    }

    // 5秒内尝试验证RFID卡
    if (xTaskGetTickCount() - verify_start_time < pdMS_TO_TICKS(5000))
    {
        if (RFID_VerifyCard(card_id))
        {
            // 验证卡片是否在存储中
            if (RFID_VerifyStoredCard(card_id))
            {
                Servo_Unlock(); // 解锁
                current_ui_state = UI_STATE_RFID_SUCCESS;
                first_entry = 1;
                OLED_Clear();
                return;
            }
        }
    }
    else
    {
        // 超时
        current_ui_state = UI_STATE_RFID_FAILED;
        first_entry = 1;
        OLED_Clear();
        return;
    }
 
    // 按*键返回
    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        char key_char = Keypad_GetChar(key);
        if (key_char == '*')
        {
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
        }
    }
}
 
static void display_add_rfid(void)
{
    static uint8_t first_entry = 1;
    static uint32_t add_start_time = 0;
    static int32_t last_left_sec = -1;
    uint8_t card_id[4];
 
    if (first_entry)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Add RFID", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Scan card...", 8);
        OLED_ShowString(0, 7, (uint8_t *)"*=Cancel", 8);
        add_start_time = xTaskGetTickCount();
        last_left_sec = -1;
        first_entry = 0;
    }

    int32_t left_sec = 5 - (int32_t)((xTaskGetTickCount() - add_start_time) / pdMS_TO_TICKS(1000));
    if (left_sec < 0) left_sec = 0;
    if (left_sec != last_left_sec)
    {
        char line[17] = {0};
        snprintf(line, sizeof(line), "Timeout: %lds", (long)left_sec);
        OLED_ShowString(0, 5, (uint8_t *)"                ", 8);
        OLED_ShowString(0, 5, (uint8_t *)line, 8);
        last_left_sec = left_sec;
    }
 
    // 5秒内尝试添加RFID卡
    if (xTaskGetTickCount() - add_start_time < pdMS_TO_TICKS(5000))
    {
        if (RFID_AddCard(card_id))
        {
            // 保存卡片到存储
            if (RFID_StoreCard(card_id))
            {
                menu_save_lock_data();
                current_ui_state = UI_STATE_RFID_SUCCESS;
                first_entry = 1;
                OLED_Clear();
                return;
            }
            else
            {
                // 存储失败，显示错误
                current_ui_state = UI_STATE_RFID_FAILED;
                first_entry = 1;
                OLED_Clear();
                return;
            }
        }
    }
    else
    {
        // 超时
        current_ui_state = UI_STATE_RFID_FAILED;
        first_entry = 1;
        OLED_Clear();
        return;
    }
    // 按*键返回
    int key = Keypad_Scan();
    if (key != KEYPAD_NO_KEY)
    {
        char key_char = Keypad_GetChar(key);
        if (key_char == '*')
        {
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            OLED_Clear();
        }
    }
}

static void display_rfid_result(void)
{
    static uint32_t result_display_time = 0;
    static uint8_t first_entry = 1;
    static uint8_t relock_done = 0;
 
    if (first_entry)
    {
        result_display_time = xTaskGetTickCount();
        first_entry = 0;
        relock_done = 0;
        OLED_Clear(); // 清屏显示结果

        if (current_ui_state == UI_STATE_RFID_SUCCESS)
        {
            Servo_Unlock();
        }
    }
 
    if (current_ui_state == UI_STATE_RFID_SUCCESS)
    {
        OLED_ShowString(0, 0, (uint8_t *)"RFID Result", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Verified", 8);
        OLED_ShowString(0, 4, (uint8_t *)"Door Unlocked", 8);
    }
    else
    {
        OLED_ShowString(0, 0, (uint8_t *)"RFID Result", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"Failed", 8);
        OLED_ShowString(0, 4, (uint8_t *)"Try Again", 8);
    }

    OLED_ShowString(0, 7, (uint8_t *)"Returning menu...", 8);

    if ((current_ui_state == UI_STATE_RFID_SUCCESS) &&
        (!relock_done) &&
        (xTaskGetTickCount() - result_display_time >= pdMS_TO_TICKS(1500)))
    {
        Servo_Lock();
        relock_done = 1;
    }
 
    // 显示2秒后返回主菜单
    if (xTaskGetTickCount() - result_display_time >= pdMS_TO_TICKS(2000))
    {
        current_ui_state = UI_STATE_MAIN_MENU;
        first_entry = 1;
        relock_done = 0;
        OLED_Clear();
    }
}

static void display_del_fingerprint(void)
{
    static uint8_t first_entry = 1;
    static uint8_t last_len = 0xFF;

    if (first_entry)
    {
        int32_t sfm_rt = menu_sfm_ensure_ready();
        if (sfm_rt != SFM_ACK_SUCCESS)
        {
            OLED_Clear();
            OLED_ShowString(0, 2, (uint8_t *)"SFM Not Ready", 8);
            OLED_ShowString(0, 4, (uint8_t *)"Check power/UART", 8);
            vTaskDelay(pdMS_TO_TICKS(2000));
            current_ui_state = UI_STATE_MAIN_MENU;
            first_entry = 1;
            return;
        }

        del_fp_id_len = 0;
        memset(del_fp_id_buf, 0, sizeof(del_fp_id_buf));
        OLED_Clear();
        last_len = 0xFF;
        first_entry = 0;
    }

    if (last_len != del_fp_id_len)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, (uint8_t *)"Delete Fingerprint", 8);
        OLED_ShowString(0, 1, (uint8_t *)"----------------", 8);
        OLED_ShowString(0, 3, (uint8_t *)"ID:", 8);
        OLED_ShowString(24, 3, (uint8_t *)del_fp_id_buf, 8);
        OLED_ShowString(0, 6, (uint8_t *)"#=Del *=Back", 8);
        OLED_ShowString(0, 7, (uint8_t *)"000=Delete All", 8);
        last_len = del_fp_id_len;
    }

    {
        int key = Keypad_Scan();
        if (key != KEYPAD_NO_KEY)
        {
            while (Keypad_Scan() != KEYPAD_NO_KEY) { vTaskDelay(pdMS_TO_TICKS(20)); }

            char key_char = Keypad_GetChar(key);

            if ((key_char >= '0') && (key_char <= '9') && (del_fp_id_len < 3))
            {
                del_fp_id_buf[del_fp_id_len++] = key_char;
                del_fp_id_buf[del_fp_id_len] = '\0';
            }
            else if (key_char == '*')
            {
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
            else if (key_char == '#')
            {
                int32_t rt;

                if (del_fp_id_len == 0)
                {
                    OLED_ShowString(0, 4, (uint8_t *)"Input ID first", 8);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    last_len = 0xFF;
                    return;
                }

                if ((del_fp_id_len == 3) && (strcmp(del_fp_id_buf, "000") == 0))
                {
                    rt = sfm_del_user_all();
                }
                else
                {
                    uint16_t del_id = (uint16_t)atoi(del_fp_id_buf);
                    rt = sfm_del_user(del_id);
                }

                OLED_Clear();
                if (rt == SFM_ACK_SUCCESS)
                {
                    OLED_ShowString(0, 3, (uint8_t *)"Delete OK", 8);
                }
                else
                {
                    OLED_ShowString(0, 3, (uint8_t *)"Delete Failed", 8);
                    OLED_ShowString(0, 5, (uint8_t *)sfm_error_code((uint8_t)rt), 8);
                }

                vTaskDelay(pdMS_TO_TICKS(1500));
                current_ui_state = UI_STATE_MAIN_MENU;
                first_entry = 1;
                OLED_Clear();
            }
        }
    }
}
