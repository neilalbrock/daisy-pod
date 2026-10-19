// Low-level glue between ST's USB device core and the STM32 HAL, for the
// Seed's own USB port (USB_OTG_FS, the micro-USB connector).
//
// Adapted from libDaisy's src/usbd/usbd_conf.c. It defines every global symbol
// that file does, so the linker never pulls libDaisy's copy out of
// libdaisy.a: libDaisy's system.cpp refers to hpcd_USB_OTG_HS and its USB host
// code to Error_Handler, and either would otherwise drag it in and clash with
// these. The changes from libDaisy's version are the start-of-frame interrupt,
// which the audio class uses as its 1 ms clock, and the FIFO sizes.
#include "usbd_core.h"

PCD_HandleTypeDef hpcd_USB_OTG_FS;
// Unused here, but libDaisy's OTG_HS interrupt handlers read it
PCD_HandleTypeDef hpcd_USB_OTG_HS;

void Error_Handler(void)
{
    while(1) {}
}

void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

static USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
    switch(hal_status)
    {
        case HAL_OK: return USBD_OK;
        case HAL_BUSY: return USBD_BUSY;
        default: return USBD_FAIL;
    }
}

/* PCD -> USB device core ---------------------------------------------------*/

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
    if(hpcd->Instance != USB_OTG_FS)
        return;

    // PA11 = DM, PA12 = DP, PA9 = VBUS sense
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin       = GPIO_PIN_12 | GPIO_PIN_11;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF10_OTG1_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin  = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    HAL_NVIC_SetPriority(OTG_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd)
{
    if(hpcd->Instance != USB_OTG_FS)
        return;
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_12 | GPIO_PIN_11 | GPIO_PIN_9);
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData,
                       (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData,
                         epnum,
                         hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage(
        (USBD_HandleTypeDef *)hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetSpeed((USBD_HandleTypeDef *)hpcd->pData, USBD_SPEED_FULL);
    USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* USB device core -> PCD ---------------------------------------------------*/

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    if(pdev->id != DEVICE_FS)
        return USBD_FAIL;

    hpcd_USB_OTG_FS.pData = pdev;
    pdev->pData           = &hpcd_USB_OTG_FS;

    hpcd_USB_OTG_FS.Instance                     = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints           = 9;
    hpcd_USB_OTG_FS.Init.speed                   = PCD_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.dma_enable              = DISABLE;
    hpcd_USB_OTG_FS.Init.phy_itface              = PCD_PHY_EMBEDDED;
    hpcd_USB_OTG_FS.Init.Sof_enable              = ENABLE;
    hpcd_USB_OTG_FS.Init.low_power_enable        = DISABLE;
    hpcd_USB_OTG_FS.Init.lpm_enable              = DISABLE;
    hpcd_USB_OTG_FS.Init.battery_charging_enable = ENABLE;
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable     = ENABLE;
    hpcd_USB_OTG_FS.Init.use_dedicated_ep1       = DISABLE;
    if(HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
        Error_Handler();

    // FIFO RAM is 1024 words (4 KB), set in 32-bit words. Transmit FIFOs are
    // per IN endpoint: 0 control, 1 feedback, 2 audio in. The receive FIFO is
    // shared by all OUT endpoints and has room for several 294-byte audio
    // packets.
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x120);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x20);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x10);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x80);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    return USBD_Get_USB_Status(HAL_PCD_DeInit(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    return USBD_Get_USB_Status(HAL_PCD_Start(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    return USBD_Get_USB_Status(HAL_PCD_Stop(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev,
                                  uint8_t             ep_addr,
                                  uint8_t             ep_type,
                                  uint16_t            ep_mps)
{
    return USBD_Get_USB_Status(
        HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type));
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return USBD_Get_USB_Status(HAL_PCD_EP_Close(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return USBD_Get_USB_Status(HAL_PCD_EP_Flush(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return USBD_Get_USB_Status(HAL_PCD_EP_SetStall(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
                                        uint8_t             ep_addr)
{
    return USBD_Get_USB_Status(HAL_PCD_EP_ClrStall(pdev->pData, ep_addr));
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
    if((ep_addr & 0x80) == 0x80)
        return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
    return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
                                         uint8_t             dev_addr)
{
    return USBD_Get_USB_Status(HAL_PCD_SetAddress(pdev->pData, dev_addr));
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev,
                                    uint8_t             ep_addr,
                                    uint8_t            *pbuf,
                                    uint32_t            size)
{
    return USBD_Get_USB_Status(
        HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size));
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                          uint8_t             ep_addr,
                                          uint8_t            *pbuf,
                                          uint32_t            size)
{
    return USBD_Get_USB_Status(
        HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size));
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}
