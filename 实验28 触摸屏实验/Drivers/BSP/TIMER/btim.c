/**
 ****************************************************************************************************
 * @file        btim.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.1
 * @date        2021-11-29
 * @brief       ������ʱ�� ��������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ̽���� F407������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 20211015
 * ��һ�η���
 * V1.1 20211129
 * ���Ӷ�ʱ��7��ʼ���������жϺ���
 ****************************************************************************************************
 */

#include "./BSP/TIMER/btim.h"


TIM_HandleTypeDef g_tim6_handler;         /* ��ʱ��������� */
TIM_HandleTypeDef g_tim7_handler;         /* ��ʱ��������� */

uint16_t frame;
__IO uint8_t frameup;

/**
 * @brief       ������ʱ��TIM6��ʱ�жϳ�ʼ������
 * @note
 *              ������ʱ����ʱ������APB1,��PPRE1 �� 2��Ƶ��ʱ��
 *              ������ʱ����ʱ��ΪAPB1ʱ�ӵ�2��, ��APB1Ϊ42M, ���Զ�ʱ��ʱ�� = 84Mhz
 *              ��ʱ�����ʱ����㷽��: Tout = ((arr + 1) * (psc + 1)) / Ft us.
 *              Ft=��ʱ������Ƶ��,��λ:Mhz
 *
 * @param       arr : �Զ���װֵ��
 * @param       psc : ʱ��Ԥ��Ƶ��
 * @retval      ��
 */
void btim_tim6_int_init(uint16_t arr, uint16_t psc)
{
    g_tim6_handler.Instance = BTIM_TIM6_INT;                /* ͨ�ö�ʱ��X */
    g_tim6_handler.Init.Prescaler = psc;                    /* ����Ԥ��Ƶ��  */
    g_tim6_handler.Init.CounterMode = TIM_COUNTERMODE_UP;   /* ���ϼ����� */
    g_tim6_handler.Init.Period = arr;                       /* �Զ�װ��ֵ */
    HAL_TIM_Base_Init(&g_tim6_handler);
    
    HAL_TIM_Base_Start_IT(&g_tim6_handler);                 /* ʹ��ͨ�ö�ʱ��x�ͼ�������жϣ�TIM_IT_UPDATE */
}

/**
 * @brief       ������ʱ��TIM7��ʱ�жϳ�ʼ������
 * @note
 *              ������ʱ����ʱ������APB1,��PPRE1 �� 2��Ƶ��ʱ��
 *              ������ʱ����ʱ��ΪAPB1ʱ�ӵ�2��, ��APB1Ϊ42M, ���Զ�ʱ��ʱ�� = 84Mhz
 *              ��ʱ�����ʱ����㷽��: Tout = ((arr + 1) * (psc + 1)) / Ft us.
 *              Ft=��ʱ������Ƶ��,��λ:Mhz
 *
 * @param       arr : �Զ���װֵ��
 * @param       psc : ʱ��Ԥ��Ƶ��
 * @retval      ��
 */
void btim_tim7_int_init(uint16_t arr, uint16_t psc)
{
    BTIM_TIM7_INT_CLK_ENABLE();                             /* ʹ��TIMʱ�� */

    g_tim7_handler.Instance = BTIM_TIM7_INT;                /* ͨ�ö�ʱ��X */
    g_tim7_handler.Init.Prescaler = psc;                    /* ����Ԥ��Ƶ��  */
    g_tim7_handler.Init.CounterMode = TIM_COUNTERMODE_UP;   /* ���ϼ����� */
    g_tim7_handler.Init.Period = arr;                       /* �Զ�װ��ֵ */
    HAL_TIM_Base_Init(&g_tim7_handler);
    
    HAL_NVIC_SetPriority(BTIM_TIM7_INT_IRQn, 1, 3);         /* ��ռ1�������ȼ�3����2 */
    HAL_NVIC_EnableIRQ(BTIM_TIM7_INT_IRQn);                 /* ����ITM3�ж� */

    HAL_TIM_Base_Start_IT(&g_tim7_handler);                 /* ʹ��ͨ�ö�ʱ��x�ͼ�������жϣ�TIM_IT_UPDATE */
}

/**
 * @brief       ��ʱ���ײ�����������ʱ�ӣ������ж����ȼ�
                �˺����ᱻHAL_TIM_Base_Init()��������
 * @param       ��
 * @retval      ��
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == BTIM_TIM6_INT)
    {
        BTIM_TIM6_INT_CLK_ENABLE();                     /* ʹ��TIMʱ�� */
        HAL_NVIC_SetPriority(BTIM_TIM6_INT_IRQn, 1, 3); /* ��ռ1�������ȼ�3����2 */
        HAL_NVIC_EnableIRQ(BTIM_TIM6_INT_IRQn);         /* ����ITM6�ж� */
    }
}


/**
 * @brief       ������ʱ��TIMX�жϷ�����
 * @param       ��
 * @retval      ��
 */
void BTIM_TIM6_INT_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_tim6_handler);  /* ��ʱ���ص����� */
}

void BTIM_TIM7_INT_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_tim7_handler);  /* ��ʱ���ص����� */
}

/**
 * @brief       �ص���������ʱ���жϷ���������
 * @param       ��
 * @retval      ��
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == (&g_tim6_handler))
    {
        frame = 0;
    }
    if (htim == (&g_tim7_handler))   /* video frame-rate tick */
    {
        frameup = 1;
    }
}

