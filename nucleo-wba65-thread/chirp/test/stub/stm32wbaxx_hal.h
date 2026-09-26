/* Host stand-ins for the ST types and calls board_serial.c uses (test only). */
#pragma once
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { HAL_UART_STATE_RESET = 0x00, HAL_UART_STATE_READY = 0x20 } HAL_UART_StateTypeDef;

typedef struct {
  struct {
    uint32_t Request, BlkHWRequest, Direction, SrcInc, DestInc, SrcDataWidth, DestDataWidth,
             Priority, SrcBurstLength, DestBurstLength, TransferAllocatedPort,
             TransferEventMode, Mode;
  } Init;
  void *Instance;
  void *Parent;
} DMA_HandleTypeDef;

typedef struct UART_HandleTypeDef {
  volatile uint32_t gState;
  volatile uint32_t RxState;
  DMA_HandleTypeDef *hdmarx;
  void (*RxEventCallback)(struct UART_HandleTypeDef *huart, uint16_t pos);
  void (*ErrorCallback)(struct UART_HandleTypeDef *huart);
} UART_HandleTypeDef;

#define GPDMA1_Channel7              ((void *)0)
#define GPDMA1_Channel7_IRQn         0
#define GPDMA1_REQUEST_USART1_RX     0u
#define DMA_BREQ_SINGLE_BURST        0u
#define DMA_PERIPH_TO_MEMORY         0u
#define DMA_SINC_FIXED               0u
#define DMA_DINC_INCREMENTED         0u
#define DMA_SRC_DATAWIDTH_BYTE       0u
#define DMA_DEST_DATAWIDTH_BYTE      0u
#define DMA_LOW_PRIORITY_HIGH_WEIGHT 0u
#define DMA_SRC_ALLOCATED_PORT0      0u
#define DMA_DEST_ALLOCATED_PORT0     0u
#define DMA_TCEM_BLOCK_TRANSFER      0u
#define DMA_NORMAL                   0u
#define DMA_CHANNEL_NPRIV            0u
#define __HAL_LINKDMA(h, field, dma) ((h)->field = &(dma))
#define __NOP()                      ((void)0)

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *h, uint32_t attr);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *buf, uint16_t size);
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub);
void HAL_NVIC_EnableIRQ(int irq);
