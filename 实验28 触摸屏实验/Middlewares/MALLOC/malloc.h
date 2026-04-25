/**
 ****************************************************************************************************
 * @file        malloc.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2021-11-04
 * @brief       �ڴ���� ����
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� STM32������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 2011104
 * ��һ�η���
 *
 ****************************************************************************************************
 */

#ifndef __MALLOC_H
#define __MALLOC_H

#include "./SYSTEM/sys/sys.h"

/* ����3���ڴ�� */
#define SRAMIN                  0                               /* �ڲ��ڴ�� */
#define SRAMCCM                 1                               /* CCM�ڴ��(�˲���SRAM����CPU���Է���!!!) */
#define SRAMEX                  2                               /* �ⲿ�ڴ�� */

#define SRAMBANK                3                               /* ����֧�ֵ�SRAM���� */


/* �����ڴ����������,������SDRAM��ʱ�򣬱���ʹ��uint32_t���ͣ�������Զ����uint16_t���Խ�ʡ�ڴ�ռ�� */
#define MT_TYPE     uint16_t


/* �����ڴ棬�ڴ������ռ�õ�ȫ���ռ��С���㹫ʽ���£�
 * size = MEM1_MAX_SIZE + (MEM1_MAX_SIZE / MEM1_BLOCK_SIZE) * sizeof(MT_TYPE)
 * ��SRAMEXΪ����size = 963 * 1024 + (963 * 1024 / 32) * 2 = 1047744 �� 1023KB

 * ��֪���ڴ�����(size)������ڴ�صļ��㹫ʽ���£�
 * MEM1_MAX_SIZE = (MEM1_BLOCK_SIZE * size) / (MEM1_BLOCK_SIZE + sizeof(MT_TYPE))
 * ��CCMΪ��, MEM2_MAX_SIZE = (32 * 64) / (32 + 2) = 60.24KB �� 60KB
 */
 
/* mem1�ڴ�����趨.mem1��ȫ�����ڲ�SRAM���� */
#define MEM1_BLOCK_SIZE         32                              /* �ڴ���СΪ32�ֽ� */
#define MEM1_MAX_SIZE           88 * 1024                       /* �������ڴ� 88K */
#define MEM1_ALLOC_TABLE_SIZE   MEM1_MAX_SIZE/MEM1_BLOCK_SIZE   /* �ڴ����С */

/* mem2�ڴ�����趨.mem2����CCM,���ڹ���CCM(�ر�ע��,�ⲿ��SRAM,��CPU���Է���!!) */
#define MEM2_BLOCK_SIZE         32                              /* �ڴ���СΪ32�ֽ� */
#define MEM2_MAX_SIZE           60 * 1024                       /* �������ڴ�60K */
#define MEM2_ALLOC_TABLE_SIZE   MEM2_MAX_SIZE/MEM2_BLOCK_SIZE   /* �ڴ����С */

/* mem3�ڴ�����趨.mem3������SRAM */
#define MEM3_BLOCK_SIZE         32                              /* �ڴ���СΪ32�ֽ� */
#define MEM3_MAX_SIZE           963 * 1024                      /* �������ڴ�963K */
#define MEM3_ALLOC_TABLE_SIZE   MEM3_MAX_SIZE/MEM3_BLOCK_SIZE   /* �ڴ����С */


/* ���û�ж���NULL, ����NULL */
#ifndef NULL
#define NULL 0
#endif


/* �ڴ���������� */
struct _m_mallco_dev
{
    void (*init)(uint8_t);              /* ��ʼ�� */
    uint16_t (*perused)(uint8_t);       /* �ڴ�ʹ���� */
    uint8_t *membase[SRAMBANK];         /* �ڴ�� ����SRAMBANK��������ڴ� */
    MT_TYPE *memmap[SRAMBANK];          /* �ڴ����״̬�� */
    uint8_t  memrdy[SRAMBANK];          /* �ڴ�����Ƿ���� */
};

extern struct _m_mallco_dev mallco_dev; /* ��mallco.c���涨�� */


/* �������� */
void my_mem_init(uint8_t memx);                             /* �ڴ������ʼ������(��/�ڲ�����) */
uint16_t my_mem_perused(uint8_t memx) ;                     /* ����ڴ�ʹ����(��/�ڲ�����) */
void my_mem_set(void *s, uint8_t c, uint32_t count);        /* �ڴ����ú��� */
void my_mem_copy(void *des, void *src, uint32_t n);         /* �ڴ濽������ */
void myfree(uint8_t memx, void *ptr);                       /* �ڴ��ͷ�(�ⲿ����) */
void *mymalloc(uint8_t memx, uint32_t size);                /* �ڴ����(�ⲿ����) */
void *myrealloc(uint8_t memx, void *ptr, uint32_t size);    /* ���·����ڴ�(�ⲿ����) */

#endif





