#include "stm32f10x.h"
void QR_UI_Init(void);
void QR_UI_Poll(void);

/* 32 samples, 32 kHz trigger: 1 kHz sine, deliberately low amplitude. */
#define BLOCK 256
#define RING 8192
static uint16_t tone[512];
static uint8_t packet[512];
static uint16_t ring[RING];
static volatile uint32_t rd, wr;
static volatile uint32_t underruns;
static volatile uint32_t playedSamples;
static volatile int receiverReset;
void Audio_CancelTransfer(void){receiverReset=1;}
uint32_t Audio_PlayedSamples(void){return playedSamples;}
static volatile int watchAudio;
uint32_t Audio_Underruns(void){return underruns;}
void Audio_ResetStats(void){underruns=0;watchAudio=0;}
unsigned Audio_Queued(void){return (unsigned)(wr-rd);}
int Storage_Downloading(void);
int Storage_Local(void);
int Storage_TakeReadTransfer(void);
void Storage_Consume(const uint8_t* data);
unsigned Audio_Free(void){return RING-(unsigned)(wr-rd);}
void Audio_Clear(void){__disable_irq();wr=rd;playedSamples=0;__enable_irq();}
void Audio_Push(const uint8_t* pcm,unsigned count,unsigned volume){unsigned i;uint32_t w=wr;for(i=0;i<count;i++){int32_t v=(int16_t)((uint16_t)pcm[i*2]|((uint16_t)pcm[i*2+1]<<8));v=v*(int32_t)volume/100;ring[(w+i)&(RING-1)]=(uint16_t)((v+32768)/16);}__DMB();wr=w+count;}
static void fill(uint32_t start) {
    uint32_t i, r = rd;
    for(i=0;i<256;i++) {
        if(r != wr) { tone[start+i] = ring[r & (RING-1)]; r++; }
        else {tone[start+i] = 2048;if(watchAudio)underruns++;}
    }
    playedSamples+=r-rd;
    rd = r;
}
void DMA2_Channel3_IRQHandler(void) {
    if(DMA_GetITStatus(DMA2_IT_HT3)) {
        DMA_ClearITPendingBit(DMA2_IT_HT3); fill(0);
    }
    if(DMA_GetITStatus(DMA2_IT_TC3)) {
        DMA_ClearITPendingBit(DMA2_IT_TC3); fill(256);
    }
}
static void receiver_init(void) {
    GPIO_InitTypeDef g;
    SPI_InitTypeDef s;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    GPIO_StructInit(&g);
    g.GPIO_Pin=GPIO_Pin_0; g.GPIO_Mode=GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB,&g); GPIO_ResetBits(GPIOB,GPIO_Pin_0);
    g.GPIO_Pin=GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_15;
    g.GPIO_Mode=GPIO_Mode_IPU; GPIO_Init(GPIOB,&g);
    SPI_StructInit(&s);
    g.GPIO_Pin=GPIO_Pin_14;g.GPIO_Mode=GPIO_Mode_AF_PP;GPIO_Init(GPIOB,&g);
    s.SPI_Direction=SPI_Direction_2Lines_FullDuplex;
    s.SPI_Mode=SPI_Mode_Slave; s.SPI_NSS=SPI_NSS_Hard;
    SPI_Init(SPI2,&s); SPI_Cmd(SPI2,ENABLE);
    SPI_I2S_DMACmd(SPI2,SPI_I2S_DMAReq_Rx,ENABLE);
}
static void receiver_recover(void){
 GPIO_ResetBits(GPIOB,GPIO_Pin_0);DMA_Cmd(DMA1_Channel4,DISABLE);DMA_Cmd(DMA1_Channel5,DISABLE);
 SPI_I2S_DeInit(SPI2);DMA_ClearFlag(DMA1_FLAG_GL4|DMA1_FLAG_GL5);receiver_init();receiverReset=0;
}
void Audio_PrepareReturn(const uint8_t* data) {
 DMA_InitTypeDef d;
 SPI_InitTypeDef s;
 GPIO_ResetBits(GPIOB,GPIO_Pin_0);
 DMA_Cmd(DMA1_Channel5,DISABLE);
 SPI_I2S_DeInit(SPI2);
 SPI_StructInit(&s);s.SPI_Direction=SPI_Direction_2Lines_FullDuplex;s.SPI_Mode=SPI_Mode_Slave;s.SPI_NSS=SPI_NSS_Hard;
 SPI_Init(SPI2,&s);
 DMA_ClearFlag(DMA1_FLAG_GL5);DMA_StructInit(&d);
 d.DMA_PeripheralBaseAddr=(uint32_t)&SPI2->DR;d.DMA_MemoryBaseAddr=(uint32_t)data;
 d.DMA_DIR=DMA_DIR_PeripheralDST;d.DMA_BufferSize=512;d.DMA_MemoryInc=DMA_MemoryInc_Enable;d.DMA_Priority=DMA_Priority_High;
 DMA_Init(DMA1_Channel5,&d);DMA_Cmd(DMA1_Channel5,ENABLE);
 SPI_I2S_DMACmd(SPI2,SPI_I2S_DMAReq_Rx|SPI_I2S_DMAReq_Tx,ENABLE);SPI_Cmd(SPI2,ENABLE);
 GPIO_SetBits(GPIOB,GPIO_Pin_0);
}
static void receive_block(void) {
    DMA_InitTypeDef d;
    uint32_t i, w;
    int32_t sample;
    /* Do not dispatch a new SPI return command before RX DMA is armed. */
    while((uint32_t)(wr-rd)>RING-BLOCK) { }
    while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12)==Bit_RESET) { }
    DMA_Cmd(DMA1_Channel4,DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL4);
    DMA_StructInit(&d);
    d.DMA_PeripheralBaseAddr=(uint32_t)&SPI2->DR;
    d.DMA_MemoryBaseAddr=(uint32_t)packet;
    d.DMA_DIR=DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize=sizeof(packet);
    d.DMA_MemoryInc=DMA_MemoryInc_Enable;
    d.DMA_Priority=DMA_Priority_High;
    DMA_Init(DMA1_Channel4,&d);
    DMA_Cmd(DMA1_Channel4,ENABLE);
    GPIO_SetBits(GPIOB,GPIO_Pin_0);
    while(!DMA_GetFlagStatus(DMA1_FLAG_TC4)) { QR_UI_Poll();if(receiverReset){receiver_recover();return;} }
    GPIO_ResetBits(GPIOB,GPIO_Pin_0);
    /* ACK ends the packet; wait for master to release NSS before any UART dispatch. */
    while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12)==Bit_RESET) { }
    if(Storage_Downloading()){Storage_Consume(packet);return;}
    if(Storage_Local())return;
    if(Storage_TakeReadTransfer()){SPI_I2S_DMACmd(SPI2,SPI_I2S_DMAReq_Tx,DISABLE);DMA_Cmd(DMA1_Channel5,DISABLE);return;}
    watchAudio=1;
    w=wr;
    for(i=0;i<BLOCK;i++) {
        sample=(int16_t)((uint16_t)packet[2*i]|((uint16_t)packet[2*i+1]<<8));
        ring[(w+i)&(RING-1)]=(uint16_t)((sample+32768)/16);
    }
    __DMB(); wr=w+BLOCK;
}

int main(void)
{
    GPIO_InitTypeDef gpio;
    DAC_InitTypeDef dac;
    DMA_InitTypeDef dma;
    TIM_TimeBaseInitTypeDef timer;
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clock;

    uint32_t i;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_SetPriority(DMA2_Channel3_IRQn, 8);
    for(i=0;i<512;i++) tone[i]=2048;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_4;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    TIM_Cmd(TIM6, DISABLE);
    TIM_TimeBaseStructInit(&timer);
    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
        timer_clock *= 2;
    timer.TIM_Prescaler = 0;
    timer.TIM_Period = (uint16_t)(timer_clock / 32000UL - 1UL);
    TIM_TimeBaseInit(TIM6, &timer);
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);

    DAC_StructInit(&dac);
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac);
    DAC_SetChannel1Data(DAC_Align_12b_R, 2048);

    DMA_DeInit(DMA2_Channel3);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R1;
    dma.DMA_MemoryBaseAddr = (uint32_t)tone;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = 512;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);
    DMA_ITConfig(DMA2_Channel3, DMA_IT_HT | DMA_IT_TC, ENABLE);
    NVIC_EnableIRQ(DMA2_Channel3_IRQn);
    DMA_Cmd(DMA2_Channel3, ENABLE);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
    QR_UI_Init();
    receiver_init();
    while (1) { receive_block(); }
}

