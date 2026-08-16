/***********************************************************************************************************************
 * File Name    : usb_paud_descriptor.c
 * Description  : USB Audio Class (UAC1) Microphone descriptor for Titan Board Mini (RA8P1).
 *
 *                UAC 1.0 audio function, capture only:
 *                  - AudioControl interface   (interface 0): IT(Microphone) -> OT(USB streaming)
 *                  - AudioStreaming interface (interface 1): isochronous IN endpoint (capture)
 *
 *                Format: 48 kHz, stereo, 16-bit PCM.
 **********************************************************************************************************************/

#include <r_usb_basic.h>
#include <r_usb_basic_api.h>
#include "r_usb_basic_cfg.h"

/******************************************************************************
 * Macro definitions
 ******************************************************************************/
/* bcdUSB / release */
#define USB_PAUD_BCDNUM            (0x0200U)
#define USB_PAUD_RELEASE           (0x0100U)
/* DCP max packet size */
#define USB_PAUD_DCPMAXP           (64U)
/* Configuration number */
#define USB_PAUD_CONFIGNUM         (1U)
/* Vendor ID / Product ID */
#define USB_PAUD_VENDORID          (0x045BU)
#define USB_PAUD_PRODUCTID         (0x5312U)

/* Audio sample format: 48 kHz / stereo / 16-bit */
#define USB_PAUD_SAMPLE_RATE       (48000U)
#define USB_PAUD_EP_PACKET_SIZE    (192U)  /* bytes per 1ms frame (stereo) */

/* Class-specific descriptor types */
#define USB_PAUD_CS_INTERFACE      (0x24U)
#define USB_PAUD_CS_ENDPOINT       (0x25U)

/* AC descriptor subtypes */
#define USB_PAUD_AC_HEADER         (0x01U)
#define USB_PAUD_AC_INPUT_TERMINAL (0x02U)
#define USB_PAUD_AC_OUTPUT_TERMINAL (0x03U)
#define USB_PAUD_AC_FEATURE_UNIT    (0x06U)

/* AS descriptor subtypes */
#define USB_PAUD_AS_GENERAL        (0x01U)
#define USB_PAUD_AS_FORMAT_TYPE    (0x02U)

/* Audio class / subclass codes */
#define USB_PAUD_IFCLS_AUDIO       (0x01U)
#define USB_PAUD_SUBCLS_AC         (0x01U)
#define USB_PAUD_SUBCLS_AS         (0x02U)

/* Terminal types */
#define USB_PAUD_TERMINAL_USB      (0x0101U)  /* USB streaming */
#define USB_PAUD_TERMINAL_MIC      (0x0201U)  /* Microphone */

/* Descriptor lengths */
#define USB_PAUD_DEVICE_LEN        (18U)
#define USB_PAUD_QUALIFIER_LEN     (10U)
#define USB_PAUD_CFG_TOTAL_LEN     (110U)
#define USB_PAUD_AC_SECTION_LEN    (40U)
#define USB_PAUD_AS_ALT1_LEN       (43U)

#define USB_PAUD_STRING0_LEN       (4U)
#define USB_PAUD_STRING1_LEN       (16U)   /* "Renesas"                 */
#define USB_PAUD_STRING2_LEN       (52U)   /* "Titan Mini USB Microphone" */

#define USB_PAUD_NUM_STRING        (3U)

/******************************************************************************
 * Exported global variables
 ******************************************************************************/

/* Standard Device Descriptor */
uint8_t g_paud_device[USB_PAUD_DEVICE_LEN] =
{
    USB_PAUD_DEVICE_LEN,                              /*  0:bLength */
    USB_DT_DEVICE,                                    /*  1:bDescriptorType */
    (uint8_t) (USB_PAUD_BCDNUM & 0xFF),               /*  2:bcdUSB_lo */
    (uint8_t) (USB_PAUD_BCDNUM >> 8),                 /*  3:bcdUSB_hi */
    0x00,                                             /*  4:bDeviceClass (defined at interface) */
    0x00,                                             /*  5:bDeviceSubClass */
    0x00,                                             /*  6:bDeviceProtocol */
    (uint8_t) USB_PAUD_DCPMAXP,                       /*  7:bMaxPacketSize0 */
    (uint8_t) (USB_PAUD_VENDORID & 0xFF),             /*  8:idVendor_lo */
    (uint8_t) (USB_PAUD_VENDORID >> 8),               /*  9:idVendor_hi */
    (uint8_t) (USB_PAUD_PRODUCTID & 0xFF),            /* 10:idProduct_lo */
    (uint8_t) (USB_PAUD_PRODUCTID >> 8),              /* 11:idProduct_hi */
    (uint8_t) (USB_PAUD_RELEASE & 0xFF),              /* 12:bcdDevice_lo */
    (uint8_t) (USB_PAUD_RELEASE >> 8),                /* 13:bcdDevice_hi */
    1,                                                /* 14:iManufacturer */
    2,                                                /* 15:iProduct */
    0,                                                /* 16:iSerialNumber */
    USB_PAUD_CONFIGNUM,                               /* 17:bNumConfigurations */
};

/* Device Qualifier */
uint8_t g_paud_qualifier[USB_PAUD_QUALIFIER_LEN] =
{
    USB_PAUD_QUALIFIER_LEN,                           /*  0:bLength */
    USB_DT_DEVICE_QUALIFIER,                          /*  1:bDescriptorType */
    (uint8_t) (USB_PAUD_BCDNUM & 0xFF),               /*  2:bcdUSB_lo */
    (uint8_t) (USB_PAUD_BCDNUM >> 8),                 /*  3:bcdUSB_hi */
    0x00,                                             /*  4:bDeviceClass */
    0x00,                                             /*  5:bDeviceSubClass */
    0x00,                                             /*  6:bDeviceProtocol */
    (uint8_t) USB_PAUD_DCPMAXP,                       /*  7:bMaxPacketSize0 */
    USB_PAUD_CONFIGNUM,                               /*  8:bNumConfigurations */
    0x00,                                             /*  9:bReserved */
};

/* Full-Speed Configuration Descriptor (UAC1: AC + AS(IN)) */
uint8_t g_paud_configuration[USB_PAUD_CFG_TOTAL_LEN] =
{
    /* ---- Configuration Descriptor ---- */
    USB_CD_BLENGTH,                                   /*  0:bLength */
    USB_DT_CONFIGURATION,                             /*  1:bDescriptorType */
    (uint8_t) (USB_PAUD_CFG_TOTAL_LEN & 0xFF),        /*  2:wTotalLength_lo */
    (uint8_t) (USB_PAUD_CFG_TOTAL_LEN >> 8),          /*  3:wTotalLength_hi */
    2,                                                /*  4:bNumInterfaces */
    USB_PAUD_CONFIGNUM,                               /*  5:bConfigurationValue */
    0,                                                /*  6:iConfiguration */
    0x80,                                             /*  7:bmAttributes (bus powered) */
    50,                                               /*  8:bMaxPower (100mA) */

    /* ---- Interface 0: AudioControl ---- */
    USB_ID_BLENGTH,                                   /*  0:bLength */
    USB_DT_INTERFACE,                                 /*  1:bDescriptorType */
    0,                                                /*  2:bInterfaceNumber */
    0,                                                /*  3:bAlternateSetting */
    0,                                                /*  4:bNumEndpoints */
    USB_PAUD_IFCLS_AUDIO,                             /*  5:bInterfaceClass: Audio */
    USB_PAUD_SUBCLS_AC,                               /*  6:bInterfaceSubClass: AudioControl */
    0,                                                /*  7:bInterfaceProtocol */
    0,                                                /*  8:iInterface */

    /* CS_INTERFACE: AC Header */
    9,                                               /*  0:bLength */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AC_HEADER,                               /*  2:bDescriptorSubtype */
    (uint8_t) (0x0100 & 0xFF),                        /*  3:bcdADC_lo */
    (uint8_t) (0x0100 >> 8),                          /*  4:bcdADC_hi */
    (uint8_t) (USB_PAUD_AC_SECTION_LEN & 0xFF),       /*  5:wTotalLength_lo */
    (uint8_t) (USB_PAUD_AC_SECTION_LEN >> 8),         /*  6:wTotalLength_hi */
    1,                                                /*  7:bInCollection */
    1,                                                /*  8:baInterfaceNr[0] (AS capture) */

    /* Input Terminal 1: Microphone */
    12,                                               /*  0:bLength */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AC_INPUT_TERMINAL,                       /*  2:bDescriptorSubtype */
    1,                                                /*  3:bTerminalID */
    (uint8_t) (USB_PAUD_TERMINAL_MIC & 0xFF),         /*  4:wTerminalType_lo */
    (uint8_t) (USB_PAUD_TERMINAL_MIC >> 8),           /*  5:wTerminalType_hi */
    0,                                                /*  6:bAssocTerminal */
    2,                                                /*  7:bNrChannels (stereo) */
    0x03, 0x00,                                       /*  8-9:wChannelConfig: FL | FR */
    0,                                                /* 10:iChannelNames */
    0,                                                /* 11:iTerminal */

    /* Feature Unit 2: Mute + Volume (microphone gain) */
    10,                                               /*  0:bLength (7 + controlSize*(channels+1)) */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AC_FEATURE_UNIT,                         /*  2:bDescriptorSubtype */
    2,                                                /*  3:bUnitID */
    1,                                                /*  4:bSourceID (IT1) */
    1,                                                /*  5:bControlSize */
    0x03,                                             /*  6:bmaControls[0]: Mute | Volume (master) */
    0x03,                                             /*  7:bmaControls[1]: Mute | Volume (ch1) */
    0x03,                                             /*  8:bmaControls[2]: Mute | Volume (ch2) */
    0,                                                /*  9:iFeature */

    /* Output Terminal 3: USB streaming (capture sink) */
    9,                                                /*  0:bLength */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AC_OUTPUT_TERMINAL,                      /*  2:bDescriptorSubtype */
    3,                                                /*  3:bTerminalID */
    (uint8_t) (USB_PAUD_TERMINAL_USB & 0xFF),         /*  4:wTerminalType_lo */
    (uint8_t) (USB_PAUD_TERMINAL_USB >> 8),           /*  5:wTerminalType_hi */
    0,                                                /*  6:bAssocTerminal */
    2,                                                /*  7:bSourceID (FU2) */
    0,                                                /*  8:iTerminal */

    /* ---- Interface 1: AudioStreaming (capture), Alternate 0 ---- */
    USB_ID_BLENGTH,
    USB_DT_INTERFACE,
    1,                                                /* bInterfaceNumber */
    0,                                                /* bAlternateSetting */
    0,                                                /* bNumEndpoints */
    USB_PAUD_IFCLS_AUDIO,
    USB_PAUD_SUBCLS_AS,
    0,
    0,

    /* ---- Interface 1: AudioStreaming (capture), Alternate 1 ---- */
    USB_ID_BLENGTH,
    USB_DT_INTERFACE,
    1,                                                /* bInterfaceNumber */
    1,                                                /* bAlternateSetting */
    1,                                                /* bNumEndpoints */
    USB_PAUD_IFCLS_AUDIO,
    USB_PAUD_SUBCLS_AS,
    0,
    0,

    /* CS_INTERFACE: AS_GENERAL */
    7,                                                /*  0:bLength */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AS_GENERAL,                              /*  2:bDescriptorSubtype */
    3,                                                /*  3:bTerminalLink (OT3) */
    1,                                                /*  4:bDelay */
    (uint8_t) (0x0001 & 0xFF),                        /*  5:wFormatTag_lo: PCM */
    (uint8_t) (0x0001 >> 8),                          /*  6:wFormatTag_hi */

    /* CS_INTERFACE: FORMAT_TYPE I */
    11,                                               /*  0:bLength */
    USB_PAUD_CS_INTERFACE,                            /*  1:bDescriptorType */
    USB_PAUD_AS_FORMAT_TYPE,                          /*  2:bDescriptorSubtype */
    0x01,                                             /*  3:bFormatType: FORMAT_TYPE_I */
    2,                                                /*  4:bNrChannels (stereo) */
    2,                                                /*  5:bSubframeSize */
    16,                                               /*  6:bBitResolution */
    1,                                                /*  7:bSamFreqType: 1 frequency */
    (uint8_t) (USB_PAUD_SAMPLE_RATE & 0xFF),          /*  8:tSamFreq[0] */
    (uint8_t) ((USB_PAUD_SAMPLE_RATE >> 8) & 0xFF),   /*  9:tSamFreq[1] */
    (uint8_t) ((USB_PAUD_SAMPLE_RATE >> 16) & 0xFF),  /* 10:tSamFreq[2] */

    /* Endpoint: isochronous IN (EP1) */
    9,                                                /*  0:bLength */
    USB_DT_ENDPOINT,                                  /*  1:bDescriptorType */
    USB_EP_IN | USB_EP1,                              /*  2:bEndpointAddress */
    USB_EP_ISO | 0x04,                                /*  3:bmAttributes: isochronous, async */
    (uint8_t) (USB_PAUD_EP_PACKET_SIZE & 0xFF),       /*  4:wMaxPacketSize_lo */
    (uint8_t) (USB_PAUD_EP_PACKET_SIZE >> 8),         /*  5:wMaxPacketSize_hi */
    1,                                                /*  6:bInterval (1ms) */
    0,                                                /*  7:bRefresh */
    0,                                                /*  8:bSynchAddress */

    /* CS_ENDPOINT: AS_GENERAL */
    7,                                                /*  0:bLength */
    USB_PAUD_CS_ENDPOINT,                             /*  1:bDescriptorType */
    USB_PAUD_AS_GENERAL,                              /*  2:bDescriptorSubtype */
    0x00,                                             /*  3:bmAttributes */
    0x00,                                             /*  4:bLockDelayUnits */
    0x00, 0x00,                                       /*  5-6:wLockDelay */
};

/* High-Speed configuration: same as FS (device runs at FS on USB_IP0) */
uint8_t g_paud_hs_configuration[USB_PAUD_CFG_TOTAL_LEN] =
{
    USB_CD_BLENGTH,
    USB_DT_CONFIGURATION,
    (uint8_t) (USB_PAUD_CFG_TOTAL_LEN & 0xFF),
    (uint8_t) (USB_PAUD_CFG_TOTAL_LEN >> 8),
    2,
    USB_PAUD_CONFIGNUM,
    0,
    0x80,
    50,

    /* Interface 0: AudioControl */
    USB_ID_BLENGTH,
    USB_DT_INTERFACE,
    0, 0, 0,
    USB_PAUD_IFCLS_AUDIO,
    USB_PAUD_SUBCLS_AC,
    0, 0,

    /* AC Header */
    9, USB_PAUD_CS_INTERFACE, USB_PAUD_AC_HEADER,
    (uint8_t) (0x0100 & 0xFF), (uint8_t) (0x0100 >> 8),
    (uint8_t) (USB_PAUD_AC_SECTION_LEN & 0xFF), (uint8_t) (USB_PAUD_AC_SECTION_LEN >> 8),
    1, 1,

    /* Input Terminal 1: Microphone */
    12, USB_PAUD_CS_INTERFACE, USB_PAUD_AC_INPUT_TERMINAL,
    1,
    (uint8_t) (USB_PAUD_TERMINAL_MIC & 0xFF), (uint8_t) (USB_PAUD_TERMINAL_MIC >> 8),
    0, 2, 0x03, 0x00, 0, 0,

    /* Feature Unit 2: Mute + Volume */
    10, USB_PAUD_CS_INTERFACE, USB_PAUD_AC_FEATURE_UNIT,
    2, 1, 1, 0x03, 0x03, 0x03, 0,

    /* Output Terminal 3: USB streaming */
    9, USB_PAUD_CS_INTERFACE, USB_PAUD_AC_OUTPUT_TERMINAL,
    3,
    (uint8_t) (USB_PAUD_TERMINAL_USB & 0xFF), (uint8_t) (USB_PAUD_TERMINAL_USB >> 8),
    0, 2, 0,

    /* Interface 1 alt 0 */
    USB_ID_BLENGTH, USB_DT_INTERFACE, 1, 0, 0, USB_PAUD_IFCLS_AUDIO, USB_PAUD_SUBCLS_AS, 0, 0,

    /* Interface 1 alt 1 */
    USB_ID_BLENGTH, USB_DT_INTERFACE, 1, 1, 1, USB_PAUD_IFCLS_AUDIO, USB_PAUD_SUBCLS_AS, 0, 0,
    7, USB_PAUD_CS_INTERFACE, USB_PAUD_AS_GENERAL, 3, 1, 0x01, 0x00,
    11, USB_PAUD_CS_INTERFACE, USB_PAUD_AS_FORMAT_TYPE, 0x01, 2, 2, 16, 1,
    (uint8_t) (USB_PAUD_SAMPLE_RATE & 0xFF), (uint8_t) ((USB_PAUD_SAMPLE_RATE >> 8) & 0xFF), (uint8_t) ((USB_PAUD_SAMPLE_RATE >> 16) & 0xFF),
    9, USB_DT_ENDPOINT, USB_EP_IN | USB_EP1, USB_EP_ISO | 0x04,
    (uint8_t) (USB_PAUD_EP_PACKET_SIZE & 0xFF), (uint8_t) (USB_PAUD_EP_PACKET_SIZE >> 8), 1, 0, 0,
    7, USB_PAUD_CS_ENDPOINT, USB_PAUD_AS_GENERAL, 0x00, 0x00, 0x00, 0x00,
};

/* ---- String Descriptors ---- */
uint8_t g_paud_string_descriptor0[USB_PAUD_STRING0_LEN] =
{
    USB_PAUD_STRING0_LEN,
    USB_DT_STRING,
    0x09, 0x04                       /* English (United States) */
};

uint8_t g_paud_string_descriptor1[USB_PAUD_STRING1_LEN] =
{
    USB_PAUD_STRING1_LEN, USB_DT_STRING,
    'R', 0x00, 'e', 0x00, 'n', 0x00, 'e', 0x00, 's', 0x00, 'a', 0x00, 's', 0x00,
};

uint8_t g_paud_string_descriptor2[USB_PAUD_STRING2_LEN] =
{
    USB_PAUD_STRING2_LEN, USB_DT_STRING,
    'T', 0x00, 'i', 0x00, 't', 0x00, 'a', 0x00, 'n', 0x00, ' ', 0x00,
    'M', 0x00, 'i', 0x00, 'n', 0x00, 'i', 0x00, ' ', 0x00,
    'U', 0x00, 'S', 0x00, 'B', 0x00, ' ', 0x00,
    'M', 0x00, 'i', 0x00, 'c', 0x00, 'r', 0x00, 'o', 0x00, 'p', 0x00, 'h', 0x00, 'o', 0x00, 'n', 0x00, 'e', 0x00,
};

uint8_t * g_paud_string_table[] =
{
    g_paud_string_descriptor0,
    g_paud_string_descriptor1,
    g_paud_string_descriptor2
};

/* USB descriptor table used by R_USB_Open() (via hal_data g_basic1_cfg.p_usb_reg) */
const usb_descriptor_t g_usb_descriptor =
{
    g_paud_device,               /* p_device */
    g_paud_configuration,        /* p_config_f  (Full-speed) */
    g_paud_hs_configuration,     /* p_config_h  (High-speed, not used at FS) */
    g_paud_qualifier,            /* p_qualifier */
    g_paud_string_table,         /* p_string */
    USB_PAUD_NUM_STRING          /* num_string */
};

/******************************************************************************
 * End Of File
 ******************************************************************************/
