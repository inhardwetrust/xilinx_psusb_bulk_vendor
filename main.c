

#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"

#include "xgpiops.h"
#define LED_MIO        9

 XGpioPs  Gpio;

// needed for USB
#include "xparameters.h"
#include "xusbps.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xusbps_ch9.h"

// bulk handler we use
#include "xusbps_class_device.h"

/************************** Constant Definitions *****************************/

#define ALIGNMENT_CACHELINE  __attribute__ ((aligned (32)))

#define MEMORY_SIZE (64 * 1024)
#ifdef __ICCARM__
#pragma data_alignment = 32
u8 Buffer[MEMORY_SIZE];
#pragma data_alignment = 4
#else
u8 Buffer[MEMORY_SIZE] ALIGNMENT_CACHELINE;
#endif

/************************** Function Prototypes ******************************/

static int UsbIntrExample(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		u16 UsbDeviceId, u16 UsbIntrId);

static void UsbIntrHandler(void *CallBackRef, u32 Mask);
static void XUsbPs_Ep0EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data);
static void XUsbPs_Ep1EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data);
static int UsbSetupIntrSystem(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		u16 UsbIntrId);
static void UsbDisableIntrSystem(XScuGic *IntcInstancePtr, u16 UsbIntrId);

/************************** Variable Definitions *****************************/

static XScuGic IntcInstance; /* IRQ Controller */
static XUsbPs UsbInstance; /* USB Controller */

static volatile int NumIrqs = 0;
static volatile int NumReceivedFrames = 0;
static XUsbPs_Local UsbLocal;


//// init buffer

static u8 g_bulk512[512] ALIGNMENT_CACHELINE;

void App_OnConfigured(XUsbPs *ip)
{
    // заполняем чем угодно
    for (int i = 0; i < 512; ++i) g_bulk512[i] = (u8)i;
    Xil_DCacheFlushRange((UINTPTR)g_bulk512, 512);

    // кладём буфер в IN-очередь EP1
    (void)XUsbPs_EpBufferSend(ip, 1, g_bulk512, 512);
}



int main(void) {
	init_platform();
	//GPIO init
	XGpioPs_Config *GpioCfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
	    if (!GpioCfg) return -1;
	    if (XGpioPs_CfgInitialize(&Gpio, GpioCfg, GpioCfg->BaseAddr) != XST_SUCCESS) return -1;
	    XGpioPs_SetDirectionPin(&Gpio, LED_MIO, 1);
	    XGpioPs_SetOutputEnablePin(&Gpio, LED_MIO, 1);
	    XGpioPs_WritePin(&Gpio, LED_MIO, 1);

	print("Hello World\n\r");
	xil_printf("SET_LED called\n");

	UsbLocal.CurrentConfig = 0;
	UsbInstance.UserDataPtr = &UsbLocal;

	int Status = UsbIntrExample(&IntcInstance, &UsbInstance,
	XPAR_XUSBPS_0_DEVICE_ID,
	XPAR_XUSBPS_0_INTR);
	if (Status != XST_SUCCESS) {
		xil_printf("UsbIntrExample failed: %d\r\n", Status);
		cleanup_platform();
		return XST_FAILURE;
	}

	while (1) {
		/* main loop */
	}
}

/*****************************************************************************/
static int UsbIntrExample(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		u16 UsbDeviceId, u16 UsbIntrId) {
	int Status;
	u8 *MemPtr = NULL;
	int ReturnStatus = XST_FAILURE;

	const u8 NumEndpoints = 2;

	XUsbPs_Config *UsbConfigPtr;
	XUsbPs_DeviceConfig DeviceConfig;

	UsbConfigPtr = XUsbPs_LookupConfig(UsbDeviceId);
	if (NULL == UsbConfigPtr) {
		goto out;
	}

	Status = XUsbPs_CfgInitialize(UsbInstancePtr, UsbConfigPtr,
			UsbConfigPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	Status = UsbSetupIntrSystem(IntcInstancePtr, UsbInstancePtr, UsbIntrId);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	DeviceConfig.EpCfg[0].Out.Type = XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].Out.NumBufs = 2;
	DeviceConfig.EpCfg[0].Out.BufSize = 64;
	DeviceConfig.EpCfg[0].Out.MaxPacketSize = 64;
	DeviceConfig.EpCfg[0].In.Type = XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].In.NumBufs = 2;
	DeviceConfig.EpCfg[0].In.MaxPacketSize = 64;

	DeviceConfig.EpCfg[1].Out.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].Out.NumBufs = 16;
	DeviceConfig.EpCfg[1].Out.BufSize = 512;
	DeviceConfig.EpCfg[1].Out.MaxPacketSize = 512;
	DeviceConfig.EpCfg[1].In.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].In.NumBufs = 16;
	DeviceConfig.EpCfg[1].In.MaxPacketSize = 512;

	DeviceConfig.NumEndpoints = NumEndpoints;

	MemPtr = (u8 *) &Buffer[0];
	memset(MemPtr, 0, MEMORY_SIZE);
	Xil_DCacheFlushRange((unsigned int) MemPtr, MEMORY_SIZE);

	DeviceConfig.DMAMemPhys = (u32) MemPtr;

	Status = XUsbPs_ConfigureDevice(UsbInstancePtr, &DeviceConfig);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	Status = XUsbPs_IntrSetHandler(UsbInstancePtr, UsbIntrHandler, NULL,
	XUSBPS_IXR_UE_MASK);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 0,
	XUSBPS_EP_DIRECTION_OUT, XUsbPs_Ep0EventHandler, UsbInstancePtr);

	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 1,
	XUSBPS_EP_DIRECTION_OUT, XUsbPs_Ep1EventHandler, UsbInstancePtr);

	XUsbPs_IntrEnable(UsbInstancePtr, XUSBPS_IXR_UR_MASK | XUSBPS_IXR_UI_MASK);

	XUsbPs_Start(UsbInstancePtr);

	ReturnStatus = XST_SUCCESS;

	out:

	return ReturnStatus;
}

/*****************************************************************************/
static void UsbIntrHandler(void *CallBackRef, u32 Mask) {
	NumIrqs++;
}

/*****************************************************************************/
static void XUsbPs_Ep0EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data) {
	XUsbPs *InstancePtr;
	int Status;
	XUsbPs_SetupData SetupData;
	u8 *BufferPtr;
	u32 BufferLen;
	u32 Handle;

	Xil_AssertVoid(NULL != CallBackRef);
	InstancePtr = (XUsbPs *) CallBackRef;

	switch (EventType) {
	case XUSBPS_EP_EVENT_SETUP_DATA_RECEIVED:
		Status = XUsbPs_EpGetSetupData(InstancePtr, EpNum, &SetupData);
		if (XST_SUCCESS == Status) {
			(int) XUsbPs_Ch9HandleSetupPacket(InstancePtr, &SetupData);
		}
		break;

	case XUSBPS_EP_EVENT_DATA_RX:
		Status = XUsbPs_EpBufferReceive(InstancePtr, EpNum, &BufferPtr,
				&BufferLen, &Handle);
		if (XST_SUCCESS == Status) {
			XUsbPs_EpBufferRelease(Handle);
		}
		break;

	default:
		/* Unhandled event. Ignore. */
		break;
	}
}

/*****************************************************************************/
static void XUsbPs_Ep1EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data) {
	XUsbPs *InstancePtr;
	int Status;
	u8 *BufferPtr;
	u32 BufferLen;
	u32 Handle;

	Xil_AssertVoid(NULL != CallBackRef);
	InstancePtr = (XUsbPs *) CallBackRef;

	switch (EventType) {
	case XUSBPS_EP_EVENT_DATA_RX: {
		Status = XUsbPs_EpBufferReceive(InstancePtr, EpNum, &BufferPtr,
				&BufferLen, &Handle);
		if (XST_SUCCESS == Status) {

			/* Cache line on Cortex is 32 */
			u32 invLen = (BufferLen + 31u) & ~31u;
			Xil_DCacheInvalidateRange((UINTPTR)BufferPtr, invLen);

			/* твой пользовательский обработчик bulk */
			XUsbPs_HandleDeviceReq(InstancePtr, EpNum, BufferPtr, BufferLen);

			XUsbPs_EpBufferRelease(Handle);
		}
		break;
	}

	default:
		/* Unhandled event. Ignore. */
		break;
	}
}

/*****************************************************************************/
static int UsbSetupIntrSystem(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		u16 UsbIntrId) {
	int Status;
	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}
	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Xil_ExceptionInit();

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler, IntcInstancePtr);

	Status = XScuGic_Connect(IntcInstancePtr, UsbIntrId,
			(Xil_ExceptionHandler) XUsbPs_IntrHandler, (void *) UsbInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	XScuGic_Enable(IntcInstancePtr, UsbIntrId);
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	return XST_SUCCESS;
}

/*****************************************************************************/
static void UsbDisableIntrSystem(XScuGic *IntcInstancePtr, u16 UsbIntrId) {
	/* Disconnect and disable the interrupt for the USB controller. */
	XScuGic_Disconnect(IntcInstancePtr, UsbIntrId);
}
