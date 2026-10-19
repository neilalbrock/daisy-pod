// Configuration for ST's USB device core, as built by common/usb_device/.
//
// This replaces libDaisy's src/usbd/usbd_conf.h, which allows only one
// interface. The Makefile puts this directory ahead of libDaisy's on the
// include path for apps that set USB_DEVICE = 1.
#pragma once

#include <stdlib.h>
#include <string.h>
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Audio control, audio streaming out, audio streaming in
#define USBD_MAX_NUM_INTERFACES 3U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_SELF_POWERED 0U

#define DEVICE_FS 0

#define USBD_malloc malloc
#define USBD_free free
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_Delay HAL_Delay

#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)

#ifdef __cplusplus
}
#endif
