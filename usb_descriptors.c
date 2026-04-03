#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"


//--------------------------------------------------------------------
// VID/PID
//--------------------------------------------------------------------

#define USB_PID   0x4014
#define USB_VID   0xCafe
#define USB_BCD   0x0200


//--------------------------------------------------------------------
// Device Descriptors
//--------------------------------------------------------------------
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}


//--------------------------------------------------------------------
// HID Report Descriptor
//--------------------------------------------------------------------

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(REPORT_ID_KEYBOARD         )),
  TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(REPORT_ID_CONSUMER_CONTROL ))
};

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
  (void) instance;
  return desc_hid_report;
}

//--------------------------------------------------------------------
// Audio Descriptor Helpers
//--------------------------------------------------------------------
// Length of the Audio Control descriptor content (the stuff INSIDE
// the Audio Control interface, describing the topology)
#define TUD_AUDIO_DESC_CTRL_TOTAL_LEN \
  ( TUD_AUDIO_DESC_CLK_SRC_LEN          /* Clock Source */             \
  + TUD_AUDIO_DESC_INPUT_TERM_LEN       /* Speaker Input Terminal */   \
  + TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN /* Speaker Feature Unit (volume/mute) */ \
  + TUD_AUDIO_DESC_OUTPUT_TERM_LEN      /* Speaker Output Terminal */  \
  + TUD_AUDIO_DESC_INPUT_TERM_LEN       /* Mic Input Terminal */       \
  + TUD_AUDIO_DESC_OUTPUT_TERM_LEN      /* Mic Output Terminal */      \
  )

// Total length of the entire audio function (IAD + all audio interfaces)
#define TUD_AUDIO_HEADSET_DESC_LEN \
  ( TUD_AUDIO_DESC_IAD_LEN                                            \
  /* Audio Control Interface */                                        \
  + TUD_AUDIO_DESC_STD_AC_LEN                                         \
  + TUD_AUDIO_DESC_CS_AC_LEN                                          \
  + TUD_AUDIO_DESC_CTRL_TOTAL_LEN                                     \
  /* Speaker Streaming Interface - Alt 0 (zero bandwidth) */           \
  + TUD_AUDIO_DESC_STD_AS_INT_LEN                                     \
  /* Speaker Streaming Interface - Alt 1 (active) */                   \
  + TUD_AUDIO_DESC_STD_AS_INT_LEN                                     \
  + TUD_AUDIO_DESC_CS_AS_INT_LEN                                      \
  + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN                                   \
  + TUD_AUDIO_DESC_STD_AS_ISO_FB_EP_LEN                                 \
  + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN                                   \
  /* Speaker Feedback Endpoint */                                      \
  + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN                                  \
  /* Mic Streaming Interface - Alt 0 (zero bandwidth) */               \
  + TUD_AUDIO_DESC_STD_AS_INT_LEN                                     \
  /* Mic Streaming Interface - Alt 1 (active) */                       \
  + TUD_AUDIO_DESC_STD_AS_INT_LEN                                     \
  + TUD_AUDIO_DESC_CS_AS_INT_LEN                                      \
  + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN                                   \
  + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN                                  \
  + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN                                   \
  )

//-------------------------------------------------------------------
// Configuration Descriptor
//-------------------------------------------------------------------

/*
enum
{
  ITF_NUM_HID,
  ITF_NUM_TOTAL
};
*/

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_HEADSET_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] =
{
  //------------- Configuration Descriptor -------------//
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  //==========================================================================
  // AUDIO FUNCTION (IAD groups the 3 audio interfaces)
  //==========================================================================

  // IAD - Interface Association Descriptor
  // Tells the OS "interfaces 0,1,2 all belong to one audio device"
  TUD_AUDIO_DESC_IAD(/*_firstitf*/ ITF_NUM_AUDIO_CONTROL,
                     /*_nitfs*/    3,  // control + spk streaming + mic streaming
                     /*_stridx*/   0x00),

  //---------- Audio Control Interface ----------//
  // Standard Audio Control interface descriptor
  TUD_AUDIO_DESC_STD_AC(/*_itfnum*/   ITF_NUM_AUDIO_CONTROL,
                        /*_nEPs*/     0,     // Audio Control uses no endpoints (control pipe only)
                        /*_stridx*/   4),    // String index for "Rotary Phone Headset"

  // Class-Specific Audio Control interface header
  // _bcdADC = 0x0200 for UAC2, _category = headset, _totallen = sum of all CS descriptors
  TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/    0x0200,
                       /*_category*/  AUDIO_FUNC_HEADSET,
                       /*_totallen*/  TUD_AUDIO_DESC_CTRL_TOTAL_LEN,
                       /*_ctrl*/      AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),

  // Clock Source: internal fixed clock
  // _attr=3 means internal programmable, _ctrl=7 means host can read freq
  TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/      UAC2_ENTITY_CLOCK,
                         /*_attr*/       3,
                         /*_ctrl*/       7,
                         /*_assocTerm*/  0x00,
                         /*_stridx*/     0x00),

  //--- Speaker Path (host -> speaker) ---//

  // Input Terminal: USB streaming (audio comes in from the host)
  TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/      UAC2_ENTITY_SPK_INPUT_TERMINAL,
                            /*_termtype*/    AUDIO_TERM_TYPE_USB_STREAMING,
                            /*_assocTerm*/   0x00,
                            /*_clkid*/       UAC2_ENTITY_CLOCK,
                            /*_nchannelslogical*/ CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX,
                            /*_channelcfg*/  AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
                            /*_idxchannelnames*/ 0x00,
                            /*_ctrl*/        0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS),
                            /*_stridx*/      0x00),

  // Feature Unit: volume + mute control for speaker
  TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(
                            /*_unitid*/      UAC2_ENTITY_SPK_FEATURE_UNIT,
                            /*_srcid*/       UAC2_ENTITY_SPK_INPUT_TERMINAL,
                            /*_ctrlch0master*/ (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS |
                                                AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS),
                            /*_ctrlch1*/     (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS |
                                              AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS),
                            /*_stridx*/      0x00),

  // Output Terminal: speaker (physical output device)
  TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/     UAC2_ENTITY_SPK_OUTPUT_TERMINAL,
                             /*_termtype*/   AUDIO_TERM_TYPE_OUT_HEADPHONES,
                             /*_assocTerm*/  UAC2_ENTITY_MIC_INPUT_TERMINAL,
                             /*_srcid*/      UAC2_ENTITY_SPK_FEATURE_UNIT,
                             /*_clkid*/      UAC2_ENTITY_CLOCK,
                             /*_ctrl*/       0x0000,
                             /*_stridx*/     0x00),

  //--- Microphone Path (microphone -> host) ---//

  // Input Terminal: microphone (physical input device)
  TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/      UAC2_ENTITY_MIC_INPUT_TERMINAL,
                            /*_termtype*/    AUDIO_TERM_TYPE_IN_GENERIC_MIC,
                            /*_assocTerm*/   UAC2_ENTITY_SPK_OUTPUT_TERMINAL,
                            /*_clkid*/       UAC2_ENTITY_CLOCK,
                            /*_nchannelslogical*/ CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX,
                            /*_channelcfg*/  AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
                            /*_idxchannelnames*/ 0x00,
                            /*_ctrl*/        0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS),
                            /*_stridx*/      0x00),

  // Output Terminal: USB streaming (audio goes out to the host)
  TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/     UAC2_ENTITY_MIC_OUTPUT_TERMINAL,
                             /*_termtype*/   AUDIO_TERM_TYPE_USB_STREAMING,
                             /*_assocTerm*/  0x00,
                             /*_srcid*/      UAC2_ENTITY_MIC_INPUT_TERMINAL,
                             /*_clkid*/      UAC2_ENTITY_CLOCK,
                             /*_ctrl*/       0x0000,
                             /*_stridx*/     0x00),

  //---------- Speaker Streaming Interface ----------//

  // Alt 0: zero bandwidth (default when audio isn't playing)
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(ITF_NUM_AUDIO_STREAMING_SPK),
                            /*_altset*/ 0x00,
                            /*_nEPs*/   0x00,
                            /*_stridx*/ 0x00),

  // Alt 1: active streaming
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(ITF_NUM_AUDIO_STREAMING_SPK),
                            /*_altset*/ 0x01,
                            /*_nEPs*/   0x02,   // data EP + feedback EP
                            /*_stridx*/ 0x00),

  // Class-Specific AS Interface: links to speaker input terminal
  TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/       UAC2_ENTITY_SPK_INPUT_TERMINAL,
                           /*_ctrl*/         AUDIO_CTRL_NONE,
                           /*_formattype*/   AUDIO_FORMAT_TYPE_I,
                           /*_formats*/      AUDIO_DATA_FORMAT_TYPE_I_PCM,
                           /*_nchannelsphysical*/ CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX,
                           /*_channelcfg*/   AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
                           /*_stridx*/       0x00),

  // Type I Format descriptor
  TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX,
                               CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX),

  // Speaker data endpoint (isochronous OUT, adaptive)
  TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/    EPNUM_AUDIO_OUT,
                               /*_attr*/  (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE | TUSB_ISO_EP_ATT_DATA),
                               /*_maxsize*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT,
                               /*_interval*/ 0x01),

  // Class-specific AS isochronous audio data endpoint
  TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
                              /*_ctrl*/ AUDIO_CTRL_NONE,
                              /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
                              /*_lockdelay*/ 0x0000),

  // Feedback endpoint (isochronous IN) - tells host our actual sample rate
  TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(EPNUM_AUDIO_FB, 4, 0x01),

  //---------- Microphone Streaming Interface ----------//

  // Alt 0: zero bandwidth
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(ITF_NUM_AUDIO_STREAMING_MIC),
                            /*_altset*/ 0x00,
                            /*_nEPs*/   0x00,
                            /*_stridx*/ 0x00),

  // Alt 1: active streaming
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(ITF_NUM_AUDIO_STREAMING_MIC),
                            /*_altset*/ 0x01,
                            /*_nEPs*/   0x01,   // just the data EP
                            /*_stridx*/ 0x00),

  // Class-Specific AS Interface: links to mic output terminal
  TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/       UAC2_ENTITY_MIC_OUTPUT_TERMINAL,
                           /*_ctrl*/         AUDIO_CTRL_NONE,
                           /*_formattype*/   AUDIO_FORMAT_TYPE_I,
                           /*_formats*/      AUDIO_DATA_FORMAT_TYPE_I_PCM,
                           /*_nchannelsphysical*/ CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX,
                           /*_channelcfg*/   AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
                           /*_stridx*/       0x00),

  // Type I Format descriptor
  TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
                               CFG_TUD_AUDIO_FUNC_1_RESOLUTION_TX),

  // Mic data endpoint (isochronous IN, async)
  TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/    EPNUM_AUDIO_IN,
                               /*_attr*/  (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA),
                               /*_maxsize*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN,
                               /*_interval*/ 0x01),

  // Class-specific AS isochronous audio data endpoint
  TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
                              /*_ctrl*/ AUDIO_CTRL_NONE,
                              /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
                              /*_lockdelay*/ 0x0000),

  //==========================================================================
  // HID INTERFACE (your existing keyboard)
  //==========================================================================
  TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                     sizeof(desc_hid_report), EPNUM_HID,
                     CFG_TUD_HID_EP_BUFSIZE, 5)
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index; // for multiple configurations
  return desc_configuration;
}

//--------------------------------------------------------------------
// String Descriptors
//--------------------------------------------------------------------

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_AUDIO_HEADSET,
};

// array of pointer to string descriptors
char const *string_desc_arr[] =
{
  (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
  "RotaryPhone",                  // 1: Manufacturer
  "Rotary Phone Keyboard+Headset",// 2: Product
  NULL,                           // 3: Serial (uses unique ID)
  "Rotary Phone Headset",         // 4: Audio function name
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  switch ( index ) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      chr_count = board_usb_get_serial(_desc_str + 1, 32);
      break;

    default:
      // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
      // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

      if ( !(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) ) return NULL;

      const char *str = string_desc_arr[index];

      // Cap at max char
      chr_count = strlen(str);
      size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
      if ( chr_count > max_count ) chr_count = max_count;

      // Convert ASCII string into UTF-16
      for ( size_t i = 0; i < chr_count; i++ ) {
        _desc_str[1 + i] = str[i];
      }
      break;
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}