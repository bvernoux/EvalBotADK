//*****************************************************************************
//
// usb_host_android.c - USB Android Open Accessory host application for Stellaris with Usb.
//
// Copyright (c) 2011 Benjamin VERNOUX
// Licensed under the GPL v2 or later, see the file gpl-2.0.txt in this archive.
//
//*****************************************************************************

#include <string.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/lm3s9b92.h"
#include "driverlib/gpio.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/udma.h"
#include "driverlib/usb.h"
#include "usblib/usblib.h"
#include "usblib/host/usbhost.h"

#include "utils/uartstdio.h"

#include "usb_android.h"

#define BULK_READ_TIMEOUT    (2)
#define BULK_WRITE_TIMEOUT    (0) /* 0=No Timeout*/

t_u8 configDesc[256];
t_u16 configDescSize = 256;

t_u8 devDesc[256];
t_u16 devDescSize = 256;

t_u8 writedata[256];

bool rx_data_available = 0;
volatile t_u32 g_ulSysTickCount = 0;

t_ident_android_accessory* id_android_accessory;

/********************************/
/* Start of ANDROID Host Driver */
/********************************/

/* USB Host Driver define to support Andoid Open Accessory
*/
#define USB_ACCESSORY_VENDOR_ID         0x18D1
#define USB_ACCESSORY_PRODUCT_ID        0x2D00
#define USB_ACCESSORY_ADB_PRODUCT_ID    0x2D01

#define ACCESSORY_STRING_MANUFACTURER   0
#define ACCESSORY_STRING_MODEL          1
#define ACCESSORY_STRING_DESCRIPTION    2
#define ACCESSORY_STRING_VERSION        3
#define ACCESSORY_STRING_URI            4
#define ACCESSORY_STRING_SERIAL         5

#define ACCESSORY_GET_PROTOCOL          51
#define ACCESSORY_SEND_STRING           52
#define ACCESSORY_START                 53

//*****************************************************************************
//
// These defines are the the events that will be passed in the \e ulEvent
// parameter of the callback from the driver.
//
//*****************************************************************************
#define ANDROID_EVENT_OPEN          1
#define ANDROID_EVENT_CLOSE         2
#define ANDROID_EVENT_RX_AVAILABLE  3

//*****************************************************************************
//
// The prototype for the USB MSC host driver callback function.
//
//*****************************************************************************
typedef void (*tUSBHANDROIDCallback)(t_u32 ulInstance, t_u32 ulEvent, void *pvEventData);

//*****************************************************************************
//
// Prototypes for the USB ANDROID host driver APIs.
//
//*****************************************************************************
void* USBHANDROIDOpen(tUSBHostDevice *pDevice);
void USBHANDROIDClose(void *pvInstance);

//*****************************************************************************
//
// This is the structure for an instance of a USB ANDROID host driver.
//
//*****************************************************************************
typedef struct
{
    // Save the connected status
    bool connected;
    
    //
    // Save the device instance.
    //
    tUSBHostDevice *pDevice;

    //
    // Used to save the callback.
    //
    tUSBHANDROIDCallback pfnCallback;
    //
    // The size of the blocks associated with this device.
    //
    t_u32 ulBlockSize;

    //
    // Bulk IN pipe.
    //
    t_u32 ulBulkInPipe;

    //
    // Bulk OUT pipe.
    //
    t_u32 ulBulkOutPipe;
} t_USBHANDROIDInstance;

//*****************************************************************************
//
// Hold the current state for the application.
//
//*****************************************************************************
typedef enum
{
    //
    // No device is present.
    //
    STATE_NO_DEVICE,

    //
    // Device is being enumerated.
    //
    STATE_DEVICE_ENUM,

    //
    // Device is ready.
    //
    STATE_DEVICE_READY,

    //
    // An unsupported device has been attached.
    //
    STATE_UNKNOWN_DEVICE,

    //
    // A power fault has occurred.
    //
    STATE_POWER_FAULT
}
tState;
volatile tState g_eState;
volatile tState g_eUIState;

//*****************************************************************************
//
// The array of USB Android host drivers.
//
//*****************************************************************************
static t_USBHANDROIDInstance g_USBHANDROIDDevice =
{
    false, /* Set status to not connected by default */
    NULL, /* pDevice is not allocated = NULL */
    NULL, /* pfnCallback  is not allocated = NULL */
    0, /* ulBlockSize = 0 */
    0, /* ulBulkInPipe = 0 */
    0 /* ulBulkOutPipe = 0 */
};

void USBHANDROIDCallback(t_u32 ulInstance, t_u32 ulEvent, void *pvData);

bool isAccessoryDevice(tDeviceDescriptor *desc)
{
    return desc->idVendor == 0x18d1 &&
    (desc->idProduct == 0x2D00 || desc->idProduct == 0x2D01);
}

int getConfigDesc(tUSBHostDevice *pDevice)
{
    tUSBRequest SetupPacket;
    t_u32 ulBytes;
    tConfigDescriptor* pconf_desc;

    ulBytes = 0;

    // This is a Standard Device IN request.
    SetupPacket.bmRequestType =
    USB_RTYPE_DIR_IN | USB_RTYPE_STANDARD | USB_RTYPE_DEVICE;

    // Request a Device Descriptor.
    SetupPacket.bRequest = USBREQ_GET_DESCRIPTOR;
    SetupPacket.wValue = USB_DTYPE_CONFIGURATION << 8;

    // Index is always 0 for device configurations requests.
    SetupPacket.wIndex = 0;

    // Save the total size and request the full configuration descriptor.
    SetupPacket.wLength = 0x09;

    // Put the setup packet in the buffer.
    ulBytes = USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
                                    (t_u8 *)&configDesc[0],
                                    SetupPacket.wLength,
                                    pDevice->DeviceDescriptor.bMaxPacketSize0);

    pconf_desc = (tConfigDescriptor*)&configDesc[0];
    UARTprintf("getConfigDesc() ctrlReq return %d bytes\n", ulBytes);
    if(ulBytes > 0)
    {
        UARTprintf("configDesc details:\n");
        UARTprintf(" bLength=0x%02X (shall be 0x09)\n", pconf_desc->bLength);
        UARTprintf(" wTotalLength=0x%04X\n", pconf_desc->wTotalLength);
        UARTprintf(" bDescriptorType=0x%02X\n", pconf_desc->bDescriptorType);
        UARTprintf(" bNumInterfaces=0x%02X\n", pconf_desc->bNumInterfaces);    
        UARTprintf(" bConfigurationValue=0x%02X\n", pconf_desc->bConfigurationValue);
        UARTprintf(" iConfiguration=0x%02X\n", pconf_desc->iConfiguration);
        UARTprintf(" bmAttributes=0x%02X\n", pconf_desc->bmAttributes);
        UARTprintf(" bMaxPower=0x%02X (unit of 2mA)\n\n", pconf_desc->bMaxPower);    
    }
    
    return(ulBytes);
}

int getDeviceDesc(tUSBHostDevice *pDevice)
{
    tUSBRequest SetupPacket;
    t_u32 ulBytes;
    tDeviceDescriptor* pdev_desc;

    ulBytes = 0;

    // This is a Standard Device IN request.
    SetupPacket.bmRequestType =
    USB_RTYPE_DIR_IN | USB_RTYPE_STANDARD | USB_RTYPE_DEVICE;

    // Request a Device Descriptor.
    SetupPacket.bRequest = USBREQ_GET_DESCRIPTOR;
    SetupPacket.wValue = USB_DTYPE_DEVICE << 8;

    // Index is always 0 for device requests.
    SetupPacket.wIndex = 0;

    // All devices must have at least an 8 t_u8 max packet size so just ask
    // for 8 bytes to start with.
    SetupPacket.wLength = 8;

    ulBytes = 0;

    // Discover the max packet size for endpoint 0.
    if(pDevice->DeviceDescriptor.bMaxPacketSize0 == 0)
    {
        // Put the setup packet in the buffer.
        ulBytes = USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
                                        (t_u8 *)&devDesc[0],
                                        sizeof(tDeviceDescriptor),
                                        MAX_PACKET_SIZE_EP0);
    }

    // Now get the full descriptor now that the actual maximum packet size
    // is known.
    if(ulBytes < sizeof(tDeviceDescriptor))
    {
        SetupPacket.wLength = (t_u16)sizeof(tDeviceDescriptor);

        ulBytes =
        USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
        (t_u8 *)&devDesc[0],
        sizeof(tDeviceDescriptor),
        pDevice->DeviceDescriptor.bMaxPacketSize0);
    }

    pdev_desc = (tDeviceDescriptor*)&devDesc[0];
    UARTprintf("getDeviceDesc() ctrlReq return %d bytes\n", ulBytes);
    if(ulBytes > 0)
    {
        UARTprintf("DeviceDesc details:\n");        
        UARTprintf(" bLength=0x%02X (shall be 0x12)\n",  pdev_desc->bLength);
        UARTprintf(" bDescriptorType=0x%02X\n", pdev_desc->bDescriptorType);
        UARTprintf(" bcdUSB=0x%02X (USB2.0=0x200)\n",  pdev_desc->bcdUSB);
        UARTprintf(" bDeviceClass=0x%02X\n",  pdev_desc->bDeviceClass);
        UARTprintf(" bDeviceSubClass=0x%02X\n", pdev_desc->bDeviceSubClass);    
        UARTprintf(" bDeviceProtocol=0x%02X\n", pdev_desc->bDeviceProtocol);
        UARTprintf(" bMaxPacketSize0=0x%02X\n", pdev_desc->bMaxPacketSize0);
        UARTprintf(" idVendor=0x%02X\n", pdev_desc->idVendor);
        UARTprintf(" idProduct=0x%02X\n", pdev_desc->idProduct);
        UARTprintf(" bcdDevice=0x%02X\n", pdev_desc->bcdDevice);
        UARTprintf(" iManufacturer=0x%02X\n", pdev_desc->iManufacturer);        
        UARTprintf(" iProduct=0x%02X\n", pdev_desc->iProduct);    
        UARTprintf(" iSerialNumber=0x%02X\n", pdev_desc->iSerialNumber);    
        UARTprintf(" bNumConfigurations=0x%02X\n\n", pdev_desc->bNumConfigurations);        
    }
    
    return(ulBytes);
}

int getProtocol(tUSBHostDevice *pDevice)
{    
    t_u16 protocol = 0xFFFF;
    tUSBRequest SetupPacket;
    t_u32 ulBytes;
    ulBytes = 0;

    // This is a Vendor Device IN request.
    SetupPacket.bmRequestType = USB_RTYPE_DIR_IN | USB_RTYPE_VENDOR | USB_RTYPE_DEVICE;
    
    // Request an Accessory Get Protocol.
    SetupPacket.bRequest = ACCESSORY_GET_PROTOCOL;
    SetupPacket.wValue = 0;
    SetupPacket.wIndex = 0;
    SetupPacket.wLength = 2;

    // Put the setup packet in the buffer.
    ulBytes = USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
                                    (t_u8 *)&protocol,
                                    SetupPacket.wLength,
                                    pDevice->DeviceDescriptor.bMaxPacketSize0);

    UARTprintf("getProtocol() ctrlReq return %d bytes, protocol:0x%02X\n", ulBytes, protocol);

    return protocol;
}

void sendString(tUSBHostDevice *pDevice, int index, const char *str)
{        
    tUSBRequest SetupPacket;
    t_u32 ulBytes;
    t_u16 wlen;     
    ulBytes = 0;

    wlen = strlen(str) + 1;

    // This is a Vendor Device OUT request.
    SetupPacket.bmRequestType = USB_RTYPE_DIR_OUT | USB_RTYPE_VENDOR | USB_RTYPE_DEVICE;

    // Request an Accessory Get Protocol.
    SetupPacket.bRequest = ACCESSORY_SEND_STRING;
    SetupPacket.wValue = 0;
    SetupPacket.wIndex = index;
    SetupPacket.wLength = wlen;

    // Put the setup packet in the buffer.
    ulBytes = USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
                                    (t_u8 *)str,
                                    wlen,
                                    pDevice->DeviceDescriptor.bMaxPacketSize0);
    
    UARTprintf("sendString() ctrlReq return %d bytes\n", ulBytes);
}

void sendStartUpAccessoryMode(tUSBHostDevice *pDevice)
{
    tUSBRequest SetupPacket;
    t_u32 ulBytes;
    ulBytes = 0;
    int nodata;    

    // This is a Vendor Device OUT request.
    SetupPacket.bmRequestType = USB_RTYPE_DIR_OUT | USB_RTYPE_VENDOR | USB_RTYPE_DEVICE;

    // Request an Accessory Get Protocol.
    SetupPacket.bRequest = ACCESSORY_START;
    SetupPacket.wValue = 0;
    SetupPacket.wIndex = 0;
    SetupPacket.wLength = 0;

    // Put the setup packet in the buffer.
    ulBytes = USBHCDControlTransfer(0, &SetupPacket, pDevice->ulAddress,
                                    (t_u8 *)&nodata,
                                    0,
                                    pDevice->DeviceDescriptor.bMaxPacketSize0);

    UARTprintf("sendStartUpAccessoryMode() ctrlReq return %d bytes\n", ulBytes);
}

bool switchDevice(tUSBHostDevice *pDevice)
{
    int protocol;
    
    UARTprintf("Start switchDevice Time=%d\n", g_ulSysTickCount);
    
    /* For Debug purpose you can also list full Configuration Descriptor & Device Descriptor */
/*
    getConfigDesc(pDevice);
    getDeviceDesc(pDevice);
*/

    protocol = getProtocol(pDevice);
    if (protocol == 1) 
    {
        UARTprintf("Device supports protocol 1\n");
    } else 
    {
        UARTprintf("Could not read device protocol version\n");
        return false;
    }

    sendString(pDevice, ACCESSORY_STRING_MANUFACTURER, id_android_accessory->manufacturer);
    sendString(pDevice, ACCESSORY_STRING_MODEL, id_android_accessory->model);
    sendString(pDevice, ACCESSORY_STRING_DESCRIPTION, id_android_accessory->description);   
    sendString(pDevice, ACCESSORY_STRING_VERSION, id_android_accessory->version);
    sendString(pDevice, ACCESSORY_STRING_URI, id_android_accessory->uri);
    sendString(pDevice, ACCESSORY_STRING_SERIAL, id_android_accessory->serial);

    sendStartUpAccessoryMode(pDevice);

    UARTprintf("End switchDevice Time=%d\n", g_ulSysTickCount);    
    
    return true;
}

//*****************************************************************************
//
// This is the callback from the ANDROID Host driver.
//
// \param ulInstance is the driver instance which is needed when communicating
// with the driver.
// \param ulEvent is one of the events defined by the driver.
// \param pvData is a pointer to data passed into the initial call to register
// the callback.
//
// This function handles callback events from the ANDROID driver.  The only events
// currently handled are the ANDROID_EVENT_OPEN and ANDROID_EVENT_CLOSE.  This allows
// the main routine to know when an ANDROID device has been detected and
// enumerated and when an ANDROID device has been removed from the system.
//
// \return Returns \e true on success or \e false on failure.
//
//*****************************************************************************
void USBHANDROIDCallback(t_u32 ulInstance, t_u32 ulEvent, void *pvData)
{
    // Determine the event.
    switch(ulEvent)
    {
        // Called when the device driver has successfully enumerated an ANDROID
        // device.
        case ANDROID_EVENT_OPEN:
        {
            UARTprintf("Android Open OK Time=%d\n", g_ulSysTickCount);    
            // Proceed to the enumeration state.
            g_eState = STATE_DEVICE_ENUM;
            break;
        }

        // Called when the device driver has been unloaded due to error or
        // the device is no longer present.
        case ANDROID_EVENT_CLOSE:
        {
            UARTprintf("Android Close OK Time=%d\n", g_ulSysTickCount);            
            // Go back to the "no device" state and wait for a new connection.
            g_eState = STATE_NO_DEVICE;
            break;
        }

        case ANDROID_EVENT_RX_AVAILABLE:
        {
            UARTprintf("Android RX avail Time=%d\n", g_ulSysTickCount);            
            rx_data_available += 1;
            break;
        }

        default:
        {
            break;
        }
    }
}

//*****************************************************************************
//
//! This function is used to open an instance of the ANDROID driver and configure it as Open Accessory.
//!
//! \param pDevice is a pointer to the device information structure.
//!
//! This function will attempt to open an instance of the ANDROID driver based on
//! the information contained in the pDevice structure.  This call can fail if
//! there are not sufficient resources to open the device.  The function will
//! return a value that should be passed back into USBANDROIDClose() when the
//! driver is no longer needed.
//!
//! \return The function will return a pointer to a ANDROID driver instance.
//
//*****************************************************************************
static void * USBHANDROIDOpen(tUSBHostDevice *pDevice)
{
    int iIdx;
    tEndpointDescriptor *pEndpointDescriptor;
    tInterfaceDescriptor *pInterface;

    UARTprintf("\nStart USBHANDROIDOpen Time=%d\n", g_ulSysTickCount);

    /* For Debug purpose you can also list full Configuration Descriptor & Device Descriptor */
    getConfigDesc(pDevice);
    getDeviceDesc(pDevice);
/*
    UARTprintf("Start pDevice->pConfigDescriptor details Time=%d:\n", g_ulSysTickCount);
    UARTprintf(" bLength=0x%02X (shall be 0x09)\n", pDevice->pConfigDescriptor->bLength);
    UARTprintf(" wTotalLength=0x%04X\n", pDevice->pConfigDescriptor->wTotalLength);
    UARTprintf(" bDescriptorType=0x%02X\n", pDevice->pConfigDescriptor->bDescriptorType);
    UARTprintf(" bNumInterfaces=0x%02X\n", pDevice->pConfigDescriptor->bNumInterfaces);    
    UARTprintf(" bConfigurationValue=0x%02X\n", pDevice->pConfigDescriptor->bConfigurationValue);
    UARTprintf(" iConfiguration=0x%02X\n", pDevice->pConfigDescriptor->iConfiguration);
    UARTprintf(" bmAttributes=0x%02X\n", pDevice->pConfigDescriptor->bmAttributes);
    UARTprintf(" bMaxPower=0x%02X (unit of 2mA)\n\n", pDevice->pConfigDescriptor->bMaxPower);

    UARTprintf("pDevice->DeviceDescriptor details:\n");
    UARTprintf(" bLength=0x%02X (shall be 0x12)\n",  pDevice->DeviceDescriptor.bLength);
    UARTprintf(" bDescriptorType=0x%02X\n",  pDevice->DeviceDescriptor.bDescriptorType);
    UARTprintf(" bcdUSB=0x%02X (USB2.0=0x200)\n",  pDevice->DeviceDescriptor.bcdUSB);
    UARTprintf(" bDeviceClass=0x%02X\n",  pDevice->DeviceDescriptor.bDeviceClass);
    UARTprintf(" bDeviceSubClass=0x%02X\n",  pDevice->DeviceDescriptor.bDeviceSubClass);    
    UARTprintf(" bDeviceProtocol=0x%02X\n",  pDevice->DeviceDescriptor.bDeviceProtocol);
    UARTprintf(" bMaxPacketSize0=0x%02X\n",  pDevice->DeviceDescriptor.bMaxPacketSize0);
    UARTprintf(" idVendor=0x%02X\n",  pDevice->DeviceDescriptor.idVendor);
    UARTprintf(" idProduct=0x%02X\n",  pDevice->DeviceDescriptor.idProduct);
    UARTprintf(" bcdDevice=0x%02X\n",  pDevice->DeviceDescriptor.bcdDevice);
    UARTprintf(" iManufacturer=0x%02X\n",  pDevice->DeviceDescriptor.iManufacturer);        
    UARTprintf(" iProduct=0x%02X\n",  pDevice->DeviceDescriptor.iProduct);    
    UARTprintf(" iSerialNumber=0x%02X\n",  pDevice->DeviceDescriptor.iSerialNumber);    
    UARTprintf(" bNumConfigurations=0x%02X\n",  pDevice->DeviceDescriptor.bNumConfigurations);
    UARTprintf("End pDevice->pConfigDescriptor details Time=%d:\n\n", g_ulSysTickCount);
*/
    // Don't allow the device to be opened without closing first.
    if(g_USBHANDROIDDevice.pDevice)
    {
        UARTprintf("\nUSBHANDROIDOpen return 0 device already opened\n");
        return(0);
    }

    // Save the device pointer.
    g_USBHANDROIDDevice.pDevice = pDevice;

    // Save the callback.
    // The CallBack is the driver callback for any Android ADK events.
    //
    // This function is called to open an instance of an android device.  It
    // should be called before any devices are connected to allow for proper
    // notification of android connection and disconnection. The
    // application should also provide the \e pfnCallback to be notified of Andoid
    // ADK related events like device enumeration and device removal.
    g_USBHANDROIDDevice.pfnCallback = USBHANDROIDCallback;
    
    /* Check Android Accessory device */
    if (isAccessoryDevice(&pDevice->DeviceDescriptor)) 
    {
        UARTprintf("Found Android Accessory device Time=%d\n", g_ulSysTickCount);

        // Get the interface descriptor.
        pInterface = USBDescGetInterface(pDevice->pConfigDescriptor, 0, 0);    

        // Loop through the endpoints of the device.
        for(iIdx = 0; iIdx < 3; iIdx++)
        {
            // Get the first endpoint descriptor.
             pEndpointDescriptor =
            USBDescGetInterfaceEndpoint(pInterface, iIdx,
            pDevice->ulConfigDescriptorSize);
            
            UARTprintf("\nEndpoint%d USBDescGetInterfaceEndpoint()=pEndpointDescriptor res=0x%08X\n", iIdx, pEndpointDescriptor);

            // If no more endpoints then break out.
            if(pEndpointDescriptor == 0)
            {
                break;
            }
            UARTprintf("pEndpointDescriptor details:\n");
            UARTprintf(" bLength=0x%02X (shall be 0x07)\n", pEndpointDescriptor->bLength);
            UARTprintf(" bDescriptorType=0x%02X\n", pEndpointDescriptor->bDescriptorType);
            UARTprintf(" bEndpointAddress=0x%02X\n", pEndpointDescriptor->bEndpointAddress);
            UARTprintf(" bmAttributes=0x%02X (0x00=CTRL, 0x01=ISOC, 0x02=BULK, 0x03=INT\n", pEndpointDescriptor->bmAttributes);
            UARTprintf(" wMaxPacketSize=0x%04X\n", pEndpointDescriptor->wMaxPacketSize);
            UARTprintf(" bInterval=0x%04X\n", pEndpointDescriptor->bInterval);

            // See if this is a bulk endpoint.
            if((pEndpointDescriptor->bmAttributes & USB_EP_ATTR_TYPE_M) ==
                    USB_EP_ATTR_BULK)
            {
                // See if this is bulk IN or bulk OUT.
                if(pEndpointDescriptor->bEndpointAddress & USB_EP_DESC_IN)
                {
                    UARTprintf("Endpoint Bulk In alloc USB Pipe\n");
                    // Allocate the USB Pipe for this Bulk IN endpoint.
                    g_USBHANDROIDDevice.ulBulkInPipe = USBHCDPipeAllocSize(0, USBHCD_PIPE_BULK_IN_DMA,
                                                                           pDevice->ulAddress,
                                                                           pEndpointDescriptor->wMaxPacketSize,
                                                                           0);
                    // Configure the USB pipe as a Bulk IN endpoint.
                    USBHCDPipeConfig(g_USBHANDROIDDevice.ulBulkInPipe,
                                     pEndpointDescriptor->wMaxPacketSize,
                                     BULK_READ_TIMEOUT,
                                     (pEndpointDescriptor->bEndpointAddress &
                                     USB_EP_DESC_NUM_M));
                }
                else
                {
                    UARTprintf("Endpoint Bulk OUT alloc USB Pipe\n");
                    // Allocate the USB Pipe for this Bulk OUT endpoint.
                    g_USBHANDROIDDevice.ulBulkOutPipe = USBHCDPipeAllocSize(0, USBHCD_PIPE_BULK_OUT_DMA,
                                                                            pDevice->ulAddress,
                                                                            pEndpointDescriptor->wMaxPacketSize,
                                                                            0);
                    // Configure the USB pipe as a Bulk OUT endpoint.
                    USBHCDPipeConfig(g_USBHANDROIDDevice.ulBulkOutPipe,
                                     pEndpointDescriptor->wMaxPacketSize,
                                     BULK_WRITE_TIMEOUT,
                                     (pEndpointDescriptor->bEndpointAddress &
                                     USB_EP_DESC_NUM_M));
                }
            }
        }

        // If the callback exists, call it with an Open event.
        if(g_USBHANDROIDDevice.pfnCallback != 0)
        {
            g_USBHANDROIDDevice.pfnCallback((t_u32)&g_USBHANDROIDDevice,
            ANDROID_EVENT_OPEN, 0);
        }
        
        // Set Flag isConnected
        g_USBHANDROIDDevice.connected = true;
    } else 
    {
        UARTprintf("Found possible device. switching to serial mode Time=%d\n", g_ulSysTickCount);
        switchDevice(pDevice);
        
        // Set Flag isConnected
        g_USBHANDROIDDevice.connected = false;  
    }

    UARTprintf("\nEnd USBHANDROIDOpen Time=%d\n", g_ulSysTickCount);        
    // Return the only instance of this device.
    return(&g_USBHANDROIDDevice);
}

//*****************************************************************************
//
//! This function is used to release an instance of the ANDROID driver.
//!
//! \param pvInstance is an instance pointer that needs to be released.
//!
//! This function will free up any resources in use by the ANDROID driver instance
//! that is passed in.  The \e pvInstance pointer should be a valid value that
//! was returned from a call to USBHANDROIDOpen().
//!
//! \return None.
//
//*****************************************************************************
static void USBHANDROIDClose(void *pvInstance)
{
    UARTprintf("Start USBHANDROIDClose Time=%d\n", g_ulSysTickCount);    

    // Do nothing if there is not a driver open.
    if(g_USBHANDROIDDevice.pDevice == 0)
    {
        return;
    }

    // Set Flag isConnected
    g_USBHANDROIDDevice.connected = false;

    // Reset the device pointer.
    g_USBHANDROIDDevice.pDevice = 0;

    // Free the Bulk IN pipe.
    if(g_USBHANDROIDDevice.ulBulkInPipe != 0)
    {
        UARTprintf("Endpoint Bulk In Free USB Pipe 0x%08X\n", g_USBHANDROIDDevice.ulBulkInPipe);
        USBHCDPipeFree(g_USBHANDROIDDevice.ulBulkInPipe);
    }

    // Free the Bulk OUT pipe.
    if(g_USBHANDROIDDevice.ulBulkOutPipe != 0)
    {
        UARTprintf("Endpoint Bulk OUT Free USB Pipe 0x%08X\n", g_USBHANDROIDDevice.ulBulkOutPipe);        
        USBHCDPipeFree(g_USBHANDROIDDevice.ulBulkOutPipe);
    }

    // If the callback exists then call it.
    if(g_USBHANDROIDDevice.pfnCallback != 0)
    {
        g_USBHANDROIDDevice.pfnCallback((t_u32)&g_USBHANDROIDDevice,
        ANDROID_EVENT_CLOSE, 0);
    }

    // Clear the callback indicating that the device is now closed.
    g_USBHANDROIDDevice.pfnCallback = 0;
    
    UARTprintf("End USBHANDROIDClose Time=%d\n", g_ulSysTickCount);    
}

//*****************************************************************************
//
// The number of SysTick ticks per second.
//
//*****************************************************************************
#define TICKS_PER_SECOND 1000
#define MS_PER_SYSTICK (1000 / TICKS_PER_SECOND)

//*****************************************************************************
//
// Our running system tick counter and a global used to determine the time
// elapsed since last call to GetTickms().
//
//*****************************************************************************
t_u32 g_ulLastTick;

//*****************************************************************************
//
// The size of the host controller's memory pool in bytes.
//
//*****************************************************************************
#define HCD_MEMORY_SIZE         256

//*****************************************************************************
//
// The memory pool to provide to the Host controller driver.
//
//*****************************************************************************
t_u8 g_pHCDPool[HCD_MEMORY_SIZE];

//*****************************************************************************
//
// Declare the USB Events driver interface.
//
//*****************************************************************************
DECLARE_EVENT_DRIVER(g_sUSBEventDriver, 0, 0, USBHCDEvents);

//*****************************************************************************
//
//! This constant global structure defines the Mass Storage Class Driver that
//! is provided with the USB library.
//
//*****************************************************************************
const tUSBHostClassDriver g_USBHostANDROIDClassDriver =
{
    USB_CLASS_MASS_STORAGE, /* ADK is by default Mass Storage Class */
    USBHANDROIDOpen,
    USBHANDROIDClose,
    0
};

const tUSBHostClassDriver g_USBHostANDROIDAccessoryClassDriver =
{
    USB_CLASS_VEND_SPECIFIC, /* When Android is detected as accessory Class is Vendor Specific */
    USBHANDROIDOpen,
    USBHANDROIDClose,
    0
};

//*****************************************************************************
//
// The global that holds all of the host drivers in use in the application.
// In this case, only the Android class is loaded.
//
//*****************************************************************************
static tUSBHostClassDriver const * const g_ppHostClassDrivers[] =
{
    &g_USBHostANDROIDClassDriver,
    &g_USBHostANDROIDAccessoryClassDriver,
    &g_sUSBEventDriver
};

//*****************************************************************************
//
// This global holds the number of class drivers in the g_ppHostClassDrivers
// list.
//
//*****************************************************************************
#define NUM_CLASS_DRIVERS       (sizeof(g_ppHostClassDrivers) /               \
    sizeof(g_ppHostClassDrivers[0]))

/******************************/
/* End of ANDROID Host Driver */
/******************************/

//*****************************************************************************
//
// The control table used by the uDMA controller.  This table must be aligned
// to a 1024 t_u8 boundary.  In this application uDMA is only used for USB,
// so only the first 6 channels are needed.
//
//*****************************************************************************
#if defined(ewarm)
#pragma data_alignment=1024
tDMAControlTable g_sDMAControlTable[6];
#elif defined(ccs)
#pragma DATA_ALIGN(g_sDMAControlTable, 1024)
tDMAControlTable g_sDMAControlTable[6];
#else
tDMAControlTable g_sDMAControlTable[6] __attribute__ ((aligned(1024)));
#endif

//*****************************************************************************
//
// The current USB operating mode - Host, Device or unknown.
//
//*****************************************************************************
tUSBMode g_eCurrentUSBMode;

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, t_u32 ulLine)
{
    while(1);
}
#endif

//*****************************************************************************
//
// This is the handler for this SysTick interrupt.
//
//*****************************************************************************
void
SysTickIntHandler(void)
{
    // Update our tick counter.
    g_ulSysTickCount++;
}

//*****************************************************************************
//
// This function returns the number of ticks since the last time this function
// was called.
//
//*****************************************************************************
static t_u32 GetTickms(void)
{
    t_u32 ulRetVal;
    t_u32 ulSaved;

    ulRetVal = g_ulSysTickCount;
    ulSaved = ulRetVal;

    if(ulSaved > g_ulLastTick)
    {
        ulRetVal = ulSaved - g_ulLastTick;
    }
    else
    {
        ulRetVal = g_ulLastTick - ulSaved;
    }

    // This could miss a few milliseconds but the timings here are on a
    // much larger scale.
    g_ulLastTick = ulSaved;

    // Return the number of milliseconds since the last time this was called.
    return(ulRetVal * MS_PER_SYSTICK);
}

void USBStackRefresh(void)
{
    USBOTGMain(GetTickms());
}

/* User function */
t_u32 GetTime_ms(void)
{
    return g_ulSysTickCount;
}

t_u32 Delta_time_ms(t_u32 start, t_u32 end)
{
    t_u32 diff;

    if(end > start)
    {
        diff=end-start;
    }else
    {
        diff=MAX_T_U32-(start-end)+1;
    }
    
    return diff;
} 

void WaitTime_ms(t_u32 wait_ms)
{
    t_u32 start, end;
    t_u32 tickms;

    start = GetTime_ms();
    
    do
    {
        end = GetTime_ms();
        tickms = Delta_time_ms(start, end);
    }while(tickms < wait_ms);
}

//*****************************************************************************
//
// USB Mode callback
//
// \param ulIndex is the zero-based index of the USB controller making the
//        callback.
// \param eMode indicates the new operating mode.
//
// This function is called by the USB library whenever an OTG mode change
// occurs and, if a connection has been made, informs us of whether we are to
// operate as a host or device.
//
// \return None.
//
//*****************************************************************************
void ModeCallback(t_u32 ulIndex, tUSBMode eMode)
{
    // Save the new mode.
    g_eCurrentUSBMode = eMode;
}

//*****************************************************************************
//
// This is the generic callback from host stack.
//
// \param pvData is actually a pointer to a tEventInfo structure.
//
// This function will be called to inform the application when a USB event has
// occurred that is outside those related to the device.  At this
// point this is used to detect unsupported devices being inserted and removed.
// It is also used to inform the application when a power fault has occurred.
// This function is required when the g_USBGenericEventDriver is included in
// the host controller driver array that is passed in to the
// USBHCDRegisterDrivers() function.
//
// \return None.
//
//*****************************************************************************
void USBHCDEvents(void *pvData)
{
    tEventInfo *pEventInfo;

    // Cast this pointer to its actual type.
    pEventInfo = (tEventInfo *)pvData;

    switch(pEventInfo->ulEvent)
    {
        case USB_EVENT_CONNECTED:
        {
            UARTprintf("Unknown connected\n");
            // An unknown device was detected.
            g_eState = STATE_UNKNOWN_DEVICE;

            break;
        }

        case USB_EVENT_DISCONNECTED:
        {
            UARTprintf("Unknown disconnected\n");
            // Unknown device has been removed.
            g_eState = STATE_NO_DEVICE;

            break;
        }

        case USB_EVENT_POWER_FAULT:
        {
            UARTprintf("Unknown PowerFault\n");
            // No power means no device is present.
            g_eState = STATE_POWER_FAULT;
            break;
        }

        default:
        {
            UARTprintf("Unknown Event\n");            
            break;
        }
    }
}

void Hardware_Init(void)
{
    // Initially wait for device connection.
    g_eState = STATE_NO_DEVICE;
    g_eUIState = STATE_NO_DEVICE;

    // Set the clocking to run from the PLL at 50MHz.
    ROM_SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |    SYSCTL_XTAL_16MHZ);

    // Initialize the UART.
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTStdioInit(0);
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    ROM_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_4 | GPIO_PIN_5);    
    
    UARTprintf("\n\nUSB Android ADK Firmware for EvalBot by titanmkd@gmail.com\n");

    /*  Configure EvalBot available Buttons (User Switch 1, 2 ...) */
    // Enable the GPIO pin to read the select button.
    ROM_SysCtlPeripheralEnable(USER_SW1_SYSCTL_PERIPH);
    ROM_GPIODirModeSet(USER_SW1_PORT_BASE, USER_SW1_PIN, GPIO_DIR_MODE_IN);
    ROM_GPIOPadConfigSet(USER_SW1_PORT_BASE, USER_SW1_PIN, GPIO_STRENGTH_2MA,
    GPIO_PIN_TYPE_STD_WPU);
    // Enable the GPIO pin to read the select button.
    ROM_SysCtlPeripheralEnable(USER_SW2_SYSCTL_PERIPH);
    ROM_GPIODirModeSet(USER_SW2_PORT_BASE, USER_SW2_PIN, GPIO_DIR_MODE_IN);
    ROM_GPIOPadConfigSet(USER_SW2_PORT_BASE, USER_SW2_PIN, GPIO_STRENGTH_2MA,
    GPIO_PIN_TYPE_STD_WPU);
    
    // Enable the GPIO pin to read the select button.
    // Enable the GPIO ports used for the bump sensors.
    ROM_SysCtlPeripheralEnable(BUMP_L_SW3_SYSCTL_PERIPH);    
    ROM_GPIODirModeSet(BUMP_L_SW3_PORT_BASE, BUMP_L_SW3_PIN, GPIO_DIR_MODE_IN);
    ROM_GPIOPadConfigSet(BUMP_L_SW3_PORT_BASE, BUMP_L_SW3_PIN, GPIO_STRENGTH_2MA,
                        GPIO_PIN_TYPE_STD_WPU);
    // Enable the GPIO pin to read the select button.
    ROM_SysCtlPeripheralEnable(BUMP_R_SW4_SYSCTL_PERIPH);    
    ROM_GPIODirModeSet(BUMP_R_SW4_PORT_BASE, BUMP_R_SW4_PIN, GPIO_DIR_MODE_IN);
    ROM_GPIOPadConfigSet(BUMP_R_SW4_PORT_BASE, BUMP_R_SW4_PIN, GPIO_STRENGTH_2MA,
                        GPIO_PIN_TYPE_STD_WPU);

    // Enable the GPIO pin for the LED1 (PF4) & LED2 (PF5) 
    // Set the direction as output, and enable the GPIO pin for digital function.
    ROM_SysCtlPeripheralEnable(LED1_SYSCTL_PERIPH);      
    GPIOPinTypeGPIOOutput(LED1_PORT_BASE, LED1_PIN);
    ROM_SysCtlPeripheralEnable(LED2_SYSCTL_PERIPH);  
    GPIOPinTypeGPIOOutput(LED2_PORT_BASE, LED2_PIN);

    // Configure SysTick for a 1KHz interrupt.
    ROM_SysTickPeriodSet(ROM_SysCtlClockGet() / TICKS_PER_SECOND);
    ROM_SysTickEnable();
    ROM_SysTickIntEnable();

    // Enable Clocking to the USB controller.
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_USB0);

    // Set the USB pins to be controlled by the USB controller.
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_USB0);
    
    GPIOPinConfigure(GPIO_PA6_USB0EPEN);
    ROM_GPIOPinTypeUSBDigital(GPIO_PORTA_BASE, GPIO_PIN_6);

    // Enable the GPIO pin for the USB HOST / DEVICE Selection (PB0).  Set the direction as output, and
    // enable the GPIO pin for digital function.
    GPIO_PORTB_DIR_R |= GPIO_PIN_0;    GPIO_PORTB_DEN_R |= GPIO_PIN_0;
    // PB0 = GND =USB HOST Enabled.
    GPIO_PORTB_DATA_R &= ~(GPIO_PIN_0);      

    // Enable the uDMA controller and set up the control table base.
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    ROM_uDMAEnable();
    ROM_uDMAControlBaseSet(g_sDMAControlTable);

    // Initialize the USB stack mode and pass in a mode callback.
    USBStackModeSet(0, USB_MODE_OTG, ModeCallback);

    // Register the host class drivers.
    USBHCDRegisterDrivers(0, g_ppHostClassDrivers, NUM_CLASS_DRIVERS);

    // Initialize the power configuration.  This sets the power enable signal
    // to be active high and does not enable the power fault.
    USBHCDPowerConfigInit(0, USBHCD_VBUS_AUTO_HIGH | USBHCD_VBUS_FILTER);

    // Initialize the USB controller for OTG operation with a 250ms polling
    // rate.
    // Warning if this delay is higher it cannot connect correctly to Android ADK.
    USBOTGModeInit(0, 250, g_pHCDPool, HCD_MEMORY_SIZE);

}

//*****************************************************************************
//
//! This function should be called before any devices are present to enable
//! the android device class driver.
//!
//! \return This function will return the driver instance to use for the other
//! android functions.  If there is no driver available at the time of
//! this call, this function will return zero.
//
//*****************************************************************************
t_AndroidInstance ANDROID_open(const t_ident_android_accessory* android_accessory/*in*/)
{
    // Set Android Accessory
    id_android_accessory = (t_ident_android_accessory*)android_accessory;
    
    // Return the requested device instance.
    return((t_AndroidInstance)&g_USBHANDROIDDevice);
}

//*****************************************************************************
//
//! This function should be called to release an ANDROID device instance.
//!
//! \param ulInstance is the device instance that is to be released.
//!
//! This function is called when an ANDROID is to be released in preparation
//! for shutdown or a switch to USB device mode, for example.  Following this
//! call, the drive is available for other clients who may open it again using
//! a call to ANDROIDOpen().
//!
//! \return None.
//
//*****************************************************************************
void ANDROID_close(t_AndroidInstance ulInstance)
{
    t_USBHANDROIDInstance *pANDROIDDevice;

    // Get a pointer to the device instance data from the handle.
    pANDROIDDevice = (t_USBHANDROIDInstance *)ulInstance;

    // Close the drive (if it is already open)
    USBHANDROIDClose((void *)pANDROIDDevice);

    // Clear the callback indicating that the device is now closed.
    pANDROIDDevice->pfnCallback = NULL;
}

bool ANDROID_isConnected(t_AndroidInstance handle)
{
    t_USBHANDROIDInstance *pANDROIDDevice;
    bool connected;

    // Get a pointer to the device instance data from the handle.
    pANDROIDDevice = (t_USBHANDROIDInstance *)handle;
    if(pANDROIDDevice != NULL)
    {
        connected = pANDROIDDevice->connected;
    }else
    {
        connected = false;
    }

    return connected;
}

/*
 * Todo replace this call by Asynchronous/Non Blocking
 * */
int ANDROID_read(t_AndroidInstance handle, t_u8* const buff/*out*/, const int len/*in*/)
{
    t_u16 i, nbdata;
    t_u32 ulBytes;
    t_USBHANDROIDInstance *pANDROIDDevice;

    // Get a pointer to the device instance data from the handle.
    pANDROIDDevice = (t_USBHANDROIDInstance *)handle;
    if(pANDROIDDevice != NULL)
    {
        /*
        * TODO use asynchronous transfer maybe to have better performance ...
        */
        /*
        if(rx_data_available == 0)
        {
            ulBytes = USBHCDPipeSchedule(g_USBHANDROIDDevice.ulBulkInPipe, (unsigned char*)buff, len);
            rx_data_available = len;
        }
        ulBytes = USBHCDPipeReadNonBlocking(g_USBHANDROIDDevice.ulBulkInPipe, (unsigned char*)buff, len);
        rx_data_available = rx_data_available - ulBytes;
        */
        
        /* Workaround code to ensure data are received and are not equal to 0.
        *  because USBHCDPipeRead() sometimes return size >0 (when timeout) even if there is no new data.
        * */
        for(i=0; i<len; i++)
        {
            buff[i] = 0;
        }
        nbdata = 0;
        ulBytes = USBHCDPipeRead(pANDROIDDevice->ulBulkInPipe, (unsigned char*)buff, len);
        for(i=0; i<len; i++)
        {
            if(buff[i] != 0)
            nbdata++;
        }
        // Data have not changed assume no data received
        if(nbdata == 0)
            ulBytes = 0;
    }
    else
    {
        /* Error invalid handle */
        ulBytes = 0;
    }
    
    return ulBytes;
}

int ANDROID_write(t_AndroidInstance handle, const void * const buff/*in*/, int len)
{
    t_USBHANDROIDInstance *pANDROIDDevice;
    t_u32 ulBytes;

    // Get a pointer to the device instance data from the handle.
    pANDROIDDevice = (t_USBHANDROIDInstance *)handle;
    if(pANDROIDDevice != NULL)
    {    
        ulBytes = USBHCDPipeWrite(pANDROIDDevice->ulBulkOutPipe, (unsigned char*)buff, len);
    }else
    {
        /* Error invalid handle */
        ulBytes = 0;
    }

    return ulBytes;
}

