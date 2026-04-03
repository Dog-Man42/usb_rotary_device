/*
 * TinyUSB Configuration - HID Keyboard + UAC2 Headset Composite Device
 *
 * This configures a composite USB device with:
 *   - HID keyboard (rotary dial T9 input)
 *   - UAC2 audio headset (speaker out + microphone in)
 *
 * Based on TinyUSB's uac2_headset example, adapted for the Pico.
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_HID               1
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_AUDIO             1
#define CFG_TUD_VENDOR            0

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    16

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------
// These defines tell the TinyUSB audio driver about our audio setup.
// We want a simple mono headset: 1ch speaker (OUT) + 1ch mic (IN).
// Sample rate: 16000 Hz (telephone quality, good for a rotary phone!)
// Resolution: 16-bit PCM
//
// NOTE: For your rotary phone handset, mono 16kHz is perfect. The
// carbon microphone and earpiece speaker are both mono devices, and
// 16kHz gives good voice quality while keeping bandwidth low.
// You could bump to 48000 if you want better quality, but 16000
// is more authentic for the telephone aesthetic.
//--------------------------------------------------------------------

#if CFG_TUD_AUDIO

// Total length of audio function descriptor (IAD + all audio interfaces)
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                223

// Number of Standard AS Interface Descriptors (2 for speaker + 2 for mic)
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT               4

//Number of audio functions (1, the headset)


//Audio Format config
#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLING_RATE   16000
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX        1
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX        1

//Bytes per sample and resolution
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX          2       // 16-bit = 2 bytes
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX          2       // 16-bit = 2 bytes
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_TX                  16
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX                  16

// Only one format
#define CFG_TUD_AUDIO_FUNC_1_N_FORMATS                      1
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX 2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX 2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX         16
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX         16

//Calculate endpoint sizes using TinyUSB helper macro
//For full-speed USB: EP size = ceil((sample_rate / 1000) + 1) * bytes_per_sample * channels
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN   \
    TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLING_RATE, \
                      CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX, \
                      CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)

#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT  \
    TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLING_RATE, \
                      CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX, \
                      CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)

//Software FIFO buffer sizes (2x endpoint size is a safe minimum)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ     (CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN * 4)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX        CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN

#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ    (CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT * 4)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX       CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT

//Enable the feedback endpoint for speaker (OUT) - helps the host
//send data at the correct rate to match our actual playback clock
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP          1

//Control request buffer size
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ         64

//Enable encoding/decoding support
#define CFG_TUD_AUDIO_ENABLE_EP_IN                1   // Mic endpoint (device -> host)
#define CFG_TUD_AUDIO_ENABLE_EP_OUT               1   // Speaker endpoint (host -> device)

//Enable type I encoding (linear PCM)
#define CFG_TUD_AUDIO_ENABLE_TYPE_I_ENCODING      1
#define CFG_TUD_AUDIO_ENABLE_TYPE_I_DECODING      1

//FIFO count for encoding/decoding (1 per channel)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_COUNT  1
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_COUNT 1

#endif // CFG_TUD_AUDIO

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
