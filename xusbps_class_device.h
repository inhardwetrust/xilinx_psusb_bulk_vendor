#ifndef XUSBPS_CLASS_DEVICE_H
#define XUSBPS_CLASS_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "xil_types.h"
#include "xusbps.h"
#include "xusbps_ch9.h"   // для XUsbPs_SetupData

/* Небольшой контекст (по желанию можно расширять) */
typedef struct {
    u32 rx_count;
    u32 tx_count;
} XUsbPs_DevCtx;

/* Обработчик BULK-данных (EP1 OUT) — вместо XUsbPs_HandleStorageReq */
void XUsbPs_HandleDeviceReq(XUsbPs *InstancePtr, u8 EpNum,
                            u8 *BufferPtr, u32 BufferLen);

/* Класс-запросы для интерфейса с классом 0xFF (обычно их нет) */
void XUsbPs_ClassReq(XUsbPs *InstancePtr, XUsbPs_SetupData *SetupData);

#ifdef __cplusplus
}
#endif
#endif /* XUSBPS_CLASS_DEVICE_H */
