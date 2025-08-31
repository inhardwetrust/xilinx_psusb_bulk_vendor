/******************************************************************************
 * Minimal vendor-device class layer for Zynq USBPS (bulk EP1 IN/OUT).
 * Replaces mass-storage handlers with simple echo / test commands.
 ******************************************************************************/

#include <string.h>
#include <stdio.h>
#include "xusbps.h"
#include "xusbps_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"

#include "xusbps_class_device.h"

/* align for cache */
#ifndef ALIGNMENT_CACHELINE
#define ALIGNMENT_CACHELINE __attribute__((aligned(32)))
#endif

#ifndef DCACHE_INVALIDATE_SIZE
#define DCACHE_INVALIDATE_SIZE(a)  ((a) % 32 ? (((a)/32)*32 + 32) : (a))
#endif

/* Helper: safe send buffer to IN (EP1) */
static inline int ep1_in_send(XUsbPs *InstancePtr, const void *buf, u32 len)
{
    if (len == 0) return XUsbPs_EpBufferSend(InstancePtr, 1, NULL, 0);
    Xil_DCacheFlushRange((UINTPTR)buf, DCACHE_INVALIDATE_SIZE(len));
    return XUsbPs_EpBufferSend(InstancePtr, 1, (void*)buf, len);
}

/* ----------------------- BULK OUT worker ----------------------------- */
/* Protocol:
   - "PING"        -> "PONG\n"
   - "ECHO " + X   -> X (what after space)
   - "STAT"        -> "rx=<n> tx=<m>\n"
   - else         -> echo of all buffer back
*/
void XUsbPs_HandleDeviceReq(XUsbPs *InstancePtr, u8 EpNum,
                            u8 *BufferPtr, u32 BufferLen)
{
    (void)EpNum; // всегда EP1

    // REach/create context (if you want  — put the pointer to UserDataPtr)
    static XUsbPs_DevCtx dev ALIGNMENT_CACHELINE = {0};
    dev.rx_count += BufferLen;

    // Fast commands
    if (BufferLen >= 4 && !memcmp(BufferPtr, "PING", 4)) {
        static const char pong[] = "PONG\n";
        (void)ep1_in_send(InstancePtr, pong, sizeof(pong)-1);
        dev.tx_count += (sizeof(pong)-1);
        return;
    }

    if (BufferLen >= 4 && !memcmp(BufferPtr, "STAT", 4)) {
        char msg[48] ALIGNMENT_CACHELINE;
        int n = snprintf(msg, sizeof(msg), "rx=%lu tx=%lu\n",
                         (unsigned long)dev.rx_count,
                         (unsigned long)dev.tx_count);
        if (n < 0) n = 0;
        (void)ep1_in_send(InstancePtr, msg, (u32)n);
        dev.tx_count += (u32)n;
        return;
    }

    if (BufferLen >= 5 && !memcmp(BufferPtr, "ECHO ", 5)) {
        u8 *payload = BufferPtr + 5;
        u32 plen = BufferLen - 5;
        (void)ep1_in_send(InstancePtr, payload, plen);
        dev.tx_count += plen;
        return;
    }

    /* by default — echo of all received data */
    (void)ep1_in_send(InstancePtr, BufferPtr, BufferLen);
    dev.tx_count += BufferLen;
}

/* -------------------- Class-specific control requests -------------------- */
/* For class interface 0xFF there are no «class requests».
   Behaviour:
   - all unknown class-requests → STALL IN
*/
void XUsbPs_ClassReq(XUsbPs *InstancePtr, XUsbPs_SetupData *SetupData)
{
    (void)SetupData;
    /* If you want to make own class-bRequest — parse it here. */
    XUsbPs_EpStall(InstancePtr, 0, XUSBPS_EP_DIRECTION_IN);
}
