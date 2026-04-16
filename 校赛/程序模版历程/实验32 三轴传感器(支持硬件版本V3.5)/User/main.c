/**
 ****************************************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-06-03
 * @brief       三轴加速度计 实验
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 探索者 F407开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./USMART/usmart.h"
#include "./BSP/KEY/key.h"
#include "./BSP/24CXX/24cxx.h"
#include "./BSP/QMA6100P/qma6100p.h"
#include <math.h>


/**
 * @brief       显示原始数据
 * @param       x, y : 坐标
 * @param       title: 标题
 * @param       val  : 值
 * @retval      无
 */
void user_show_mag(uint16_t x, uint16_t y, char *title, float val)
{
    char buf[20];

    sprintf(buf,"%s%3.1f", title, val);                 /* 格式化输出 */
    lcd_fill(x + 30, y + 16, x + 160, y + 16, WHITE);   /* 清除上次数据(最多显示20个字符,20*8=160) */
    lcd_show_string(x, y, 160, 16, 16, buf, BLUE);      /* 显示字符串 */
}

/**
 * @brief       加速度传感器校验函数
 *   @note      此函数用于校验QMA6100P加速度传感器的校准状态
 *              进入此函数后，请将开发板依次保持以下6个方向各2秒：
 *              1. 正面朝上水平放置
 *              2. 正面朝下水平放置
 *              3. 左侧朝上垂直放置
 *              4. 右侧朝上垂直放置
 *              5. 顶部朝上垂直放置
 *              6. 底部朝上垂直放置
 *              完成后按WKUP键退出并显示校验结果
 * @param       无
 * @retval      无
 */
void accel_calibration_verify(void)
{
    uint8_t key = 0;
    uint8_t step = 0;
    uint32_t last_time = 0;
    uint32_t current_time = 0;
    
    qma6100p_rawdata_t xyz_calibrationdata;
    
    /* 校验结果变量 */
    float x_min = 0, x_max = 0;
    float y_min = 0, y_max = 0;
    float z_min = 0, z_max = 0;
    
    lcd_clear(WHITE);
    lcd_show_string(10, 50, 240, 16, 16, "Accel Calibration Verify", RED);
    lcd_show_string(10, 70, 240, 16, 16, "Place board in 6 positions:", RED);
    lcd_show_string(10, 90, 240, 16, 16, "1. Face up", RED);
    lcd_show_string(10, 110, 240, 16, 16, "2. Face down", RED);
    lcd_show_string(10, 130, 240, 16, 16, "3. Left side up", RED);
    lcd_show_string(10, 150, 240, 16, 16, "4. Right side up", RED);
    lcd_show_string(10, 170, 240, 16, 16, "5. Top up", RED);
    lcd_show_string(10, 190, 240, 16, 16, "6. Bottom up", RED);
    lcd_show_string(10, 210, 240, 16, 16, "Press WKUP when done", RED);
    
    last_time = HAL_GetTick();
    printf("last_time = %d\r\n",last_time);
    
    while (1)
    {
        key = key_scan(0);
        
        if (key == WKUP_PRES)   /* 结束校验 */
        {
            break;
        }
        
        current_time = HAL_GetTick();
        printf("current_time = %d\r\n",current_time);
        
        /* 每2秒切换一次状态 */
        if ((current_time - last_time) > 2000)
        {
            step++;
            
            if (step > 5) 
            {
                step = 0;
            }
            
            last_time = current_time;
        }
        
        qma6100p_read_rawdata(&xyz_calibrationdata);    /* 读取校准后的加速度值 */
        
        /* 更新各轴极值 */
        x_max = xyz_calibrationdata.acc_x > x_max ? xyz_calibrationdata.acc_x : x_max;
        x_min = xyz_calibrationdata.acc_x < x_min ? xyz_calibrationdata.acc_x : x_min;
        y_max = xyz_calibrationdata.acc_y > y_max ? xyz_calibrationdata.acc_y : y_max;
        y_min = xyz_calibrationdata.acc_y < y_min ? xyz_calibrationdata.acc_y : y_min;
        z_max = xyz_calibrationdata.acc_z > z_max ? xyz_calibrationdata.acc_z : z_max;
        z_min = xyz_calibrationdata.acc_z < z_min ? xyz_calibrationdata.acc_z : z_min;
        
        /* 显示当前状态 */
        lcd_fill(10, 230, 240, 290, WHITE);
        switch(step)
        {
            case 0:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Face up", BLUE);
                break;
            }
            case 1:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Face down", BLUE);
                break;
            }
            case 2:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Left side up", BLUE);
                break;
            }
            case 3:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Right side up", BLUE);
                break;
            }
            case 4:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Top up", BLUE);
                break;
            }
            case 5:
            {
                lcd_show_string(10, 230, 240, 16, 16, "Current: Bottom up", BLUE);
                break;
            }
        }
        
        LED0_TOGGLE();          /* LED0闪烁,提示程序运行 */
        delay_ms(50);
    }
    
    /* 显示校验结果 */
    lcd_clear(WHITE);
    lcd_show_string(10, 90, 240, 16, 16, "Accel Verify Result:", RED);
    
    /* 计算各轴偏差 */
    float x_error = (fabs(x_max - 1.0f) + fabs(x_min + 1.0f)) / 2;
    float y_error = (fabs(y_max - 1.0f) + fabs(y_min + 1.0f)) / 2;
    float z_error = (fabs(z_max - 1.0f) + fabs(z_min + 1.0f)) / 2;
    
    char buf[50];
    
    sprintf(buf, "X: %.3fg ~ %.3fg (err:%.3f)", x_min, x_max, x_error);
    lcd_show_string(10, 110, 240, 16, 16, buf, BLACK);
    
    sprintf(buf, "Y: %.3fg ~ %.3fg (err:%.3f)", y_min, y_max, y_error);
    lcd_show_string(10, 130, 240, 16, 16, buf, BLACK);
    
    sprintf(buf, "Z: %.3fg ~ %.3fg (err:%.3f)", z_min, z_max, z_error);
    lcd_show_string(10, 150, 240, 16, 16, buf, BLACK);
    
    /* 判断校准是否合格 */
    float max_error = fmaxf(x_error, fmaxf(y_error, z_error));
    
    /* 串口打印校验结果 */
    printf("Accelerometer Calibration Verify Result:\r\n");
    printf("X range: %.3fg to %.3fg (error: %.3f)\r\n", x_min, x_max, x_error);
    printf("Y range: %.3fg to %.3fg (error: %.3f)\r\n", y_min, y_max, y_error);
    printf("Z range: %.3fg to %.3fg (error: %.3f)\r\n", z_min, z_max, z_error);
    printf("Max error: %.3f - ", max_error);
    
    if (max_error < 0.1f) 
    {
        printf("Excellent!\r\n");
        lcd_show_string(10, 190, 240, 16, 16, "Calibration: Excellent!", GREEN);
    } 
    else if (max_error < 0.2f) 
    {
        printf("Good\r\n");
        lcd_show_string(10, 190, 240, 16, 16, "Calibration: Good", BLUE);
    }
    else if (max_error < 0.3f)
    {
        printf("Fair\r\n");
        lcd_show_string(10, 190, 240, 16, 16, "Calibration: Fair", YELLOW);
    } 
    else 
    {
        printf("Poor, recalibration needed!\r\n");
        lcd_show_string(10, 190, 240, 16, 16, "Calibration: Poor", RED);
    }
}

int main(void)
{
    uint8_t t;
    uint8_t key = 0;
    
    qma6100p_rawdata_t xyz_rawdata;

    HAL_Init();                             /* 初始化HAL库 */
    sys_stm32_clock_init(336, 8, 2, 7);     /* 设置时钟,168Mhz */
    delay_init(168);                        /* 延时初始化 */
    usart_init(115200);                     /* 串口初始化为115200 */
    usmart_dev.init(84);                    /* 初始化USMART */
    led_init();                             /* 初始化LED */
    key_init();                             /* 初始化按键 */
    lcd_init();                             /* 初始化LCD */
    
    while (qma6100p_init())                 /* 初始化三轴加速度计 */
    {
        lcd_show_string(30, 110, 200, 16, 16, "QMA6100P Error", RED);
        delay_ms(200);
        lcd_fill(30, 110, 239, 130 + 16, WHITE);
        delay_ms(200);
    }

RST:
    lcd_show_string(30, 50, 200, 16, 16, "STM32", RED);
    lcd_show_string(30, 70, 200, 16, 16, "QMA6100P TEST", RED);
    lcd_show_string(30, 90, 200, 16, 16, "ATOM@ALIENTEK", RED);
    lcd_show_string(30, 110, 200, 16, 16, "KEY0 to calibration", RED);
    lcd_show_string(30, 130, 200, 16, 16, " ACC_X :", RED);
    lcd_show_string(30, 150, 200, 16, 16, " ACC_Y :", RED);
    lcd_show_string(30, 170, 200, 16, 16, " ACC_Z :", RED);
    lcd_show_string(30, 190, 200, 16, 16, " Pitch :", RED);
    lcd_show_string(30, 210, 200, 16, 16, " Roll  :", RED);

    while (1)
    {
        key = key_scan(0);
        
        if (key == KEY0_PRES)               /* KEY0按下，执行校准 */
        {
            accel_calibration_verify();     /* 校准函数 */
            lcd_clear(WHITE);               /* 清屏 */
            goto RST;                       /* 校准完后,跳到RST, 重新显示提示信息 */
        }
        
        delay_ms(5);
        t++;
        
        if (t == 20)                        /* 0.2秒左右更新一次三轴原始值 */
        {   
            qma6100p_read_rawdata(&xyz_rawdata);
            
            user_show_mag(30, 130, "ACC_X :", xyz_rawdata.acc_x);
            user_show_mag(30, 150, "ACC_Y :", xyz_rawdata.acc_y);
            user_show_mag(30, 170, "ACC_Z :", xyz_rawdata.acc_z);
            user_show_mag(30, 190, "Pitch :", xyz_rawdata.pitch);
            user_show_mag(30, 210, "Roll  :", xyz_rawdata.roll);
            
            t = 0;
            LED0_TOGGLE();
        }
    }
}
