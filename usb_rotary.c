/*
 * usb_rotary.c - Main application for Rotary Phone USB Keyboard + Headset
 *
 * This file combines:
 *   1. Your existing rotary dial HID keyboard with T9 input
 *   2. NEW: UAC2 audio headset using the phone handset's speaker & microphone
 *
 * HARDWARE SETUP FOR AUDIO:
 *
 *   MICROPHONE (carbon/variable resistance mic):
 *     The carbon mic in a rotary phone is a variable-resistance element.
 *     You need a voltage divider to convert resistance changes to voltage:
 *
 *       3.3V ---[R_bias]---+---[Carbon Mic]--- GND
 *                          |
 *                        ADC pin (GP26 / ADC0)
 *
 *     R_bias should be roughly the nominal resistance of your mic element
 *     (typically 50-200 ohms for a carbon mic). Start with 100 ohms and
 *     adjust. A coupling capacitor (10uF) in series with the ADC pin can
 *     help remove DC offset, but we handle DC removal in software too.
 *
 *   SPEAKER (earpiece):
 *     The earpiece speaker is driven by PWM through a simple low-pass filter:
 *
 *       GP2 (PWM) ---[1K resistor]---+---[speaker]--- GND
 *                                    |
 *                               [100nF cap]
 *                                    |
 *                                   GND
 *
 *     The RC filter smooths the PWM into an analog-ish signal.
 *     The earpiece speaker impedance is typically 100-300 ohms, which
 *     is fine to drive directly from the Pico GPIO through a resistor.
 *
 *   If the speaker is too quiet, you can add a simple transistor amplifier
 *   (2N2222 + a couple resistors) between the PWM output and the speaker.
 */


#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

/* Blinking Pattern
 * 250ms    : Not mounted
 * 1000ms   : Mounted
 * 2000ms   : Suspended
*/
enum {
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2000,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void init_pins(void);
void led_blinking_task(void);
void hid_task(void);
void keyboard_task(uint8_t report_id);

void audio_init(void);
bool spk_timer_callback(struct repeating_timer *t);
bool mic_timer_callback(struct repeating_timer *t);



//Pins used for the program
static const uint signal_pin = 17;
static const uint pulse_pin = 16;

static const uint pulse_delay = 2;

static const uint mic_adc_pin = 26;     // GP26 = ADC0 input
static const uint mic_adc_input = 0;    // ADC input 0
static const uint spk_pwm_pin = 2;      // GP2 = PWM1A output

//--------------------------------------------------------------------
// Audio State
//--------------------------------------------------------------------
#define AUDIO_SAMPLE_RATE     16000

// Circular buffer for speaker playback data
#define SPK_BUF_SIZE          1024    // Must be power of 2
static int16_t spk_buf[SPK_BUF_SIZE];
static volatile uint32_t spk_rd_idx = 0;
static volatile uint32_t spk_wr_idx = 0;

// Mic sampling state
#define MIC_BUF_SIZE 1024
static int16_t mic_buf[MIC_BUF_SIZE];
static volatile uint32_t mic_rd_idx = 0;
static volatile uint32_t mic_wr_idx = 0;
static int32_t dc_offset = 2048 * 16;  // moved from callback to global


static volatile bool audio_active_mic = false;
static volatile bool audio_active_spk = false;

// Speaker volume/mute (controlled by host)
static int16_t spk_volume = 0;       // In 1/256 dB units
static bool spk_muted = false;

// Mic volume/mute
static int16_t mic_volume = 0;
static bool mic_muted = false;

// Current sample rate (reported to host)
static uint32_t current_sample_rate = AUDIO_SAMPLE_RATE;

// Supported sample rates
static const uint32_t sample_rates[] = { 16000 };
static const uint8_t n_sample_rates = sizeof(sample_rates) / sizeof(sample_rates[0]);

static struct repeating_timer mic_timer;
static struct repeating_timer spk_timer;

//--------------------------------------------------------------------
// Audio Hardware Init
//--------------------------------------------------------------------

void audio_init(void) {
    // --- Microphone ADC Setup ---
    adc_init();
    adc_gpio_init(mic_adc_pin);
    adc_select_input(mic_adc_input);

    // --- Speaker PWM Setup ---
    gpio_set_function(spk_pwm_pin, GPIO_FUNC_PWM);
    uint spk_pwm_slice = pwm_gpio_to_slice_num(spk_pwm_pin);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, 1023);     // 10-bit PWM resolution
    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_init(spk_pwm_slice, &config, true);

    pwm_set_gpio_level(spk_pwm_pin, 512);  // silence at midpoint

    // Start a repeating timer at 16kHz (every 62.5 microseconds)
    // Negative value means the interval is exact regardless of callback duration
    add_repeating_timer_us(-62, spk_timer_callback, NULL, &spk_timer);
    add_repeating_timer_us(-62, mic_timer_callback, NULL, &mic_timer);
}


int main() {

    board_init();
    init_pins();
    audio_init();


    tud_init(BOARD_TUD_RHPORT);
    
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    //Main loop
    while(1) {
        //Tinyusb device task
        tud_task();

        led_blinking_task();
        
        hid_task();


    }
}

//Initializes the GPIO pins used
void init_pins(){
    gpio_init(signal_pin);
    gpio_set_dir(signal_pin, GPIO_IN);

    gpio_init(pulse_pin);
    gpio_set_dir(pulse_pin, GPIO_IN);

}


//---------------------------------
//  Device Callbacks
//---------------------------------

// Invoked when device is mounted
void tud_mount_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_unmount_cb(void) {
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspened
// remote_wakeup_en : if host allow us to perform remote wakeip
// Within 7ms, the device must draw an average current of less than 2.5mA from the bus
void tud_suspend_cb(bool remote_wakeup_en) {
    blink_interval_ms = BLINK_SUSPENDED;

}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {

    //If the device is mounted, set to BLINK_MOUNTED, if not, set to BLINK_NOT_MOUNTED
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//======================================================================
//  AUDIO CALLBACKS - TinyUSB UAC2
//======================================================================

//--------------------------------------------------------------------
// Audio: Interface alt setting changed
// Called when the host starts/stops audio streaming
//--------------------------------------------------------------------
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex);
    uint8_t const alt = tu_u16_low(p_request->wValue);
    (void) alt;

    if (itf == ITF_NUM_AUDIO_STREAMING_SPK) {
        audio_active_spk = false;
        // Reset speaker buffer
        spk_rd_idx = 0;
        spk_wr_idx = 0;
        // Set PWM to silence
        pwm_set_gpio_level(spk_pwm_pin, 128);
    }
    if (itf == ITF_NUM_AUDIO_STREAMING_MIC) {
        audio_active_mic = false;
    }
    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex);
    uint8_t const alt = tu_u16_low(p_request->wValue);

    if (itf == ITF_NUM_AUDIO_STREAMING_SPK) {
        audio_active_spk = (alt != 0);
        if (!audio_active_spk) {
            spk_rd_idx = 0;
            spk_wr_idx = 0;
            pwm_set_gpio_level(spk_pwm_pin, 128);
        }
    }
    if (itf == ITF_NUM_AUDIO_STREAMING_MIC) {
        audio_active_mic = (alt != 0);
    }
    return true;
}

//--------------------------------------------------------------------
// Audio: Speaker data received from host
// This is called when new PCM data arrives for the speaker.
// We read it from the TinyUSB FIFO and push it into our ring buffer.
//--------------------------------------------------------------------
bool tud_audio_rx_done_post_read_cb(uint8_t rhport, uint16_t n_bytes_received,
                                     uint8_t func_id, uint8_t ep_out,
                                     uint8_t cur_alt_setting) {
    (void) rhport; (void) func_id; (void) ep_out; (void) cur_alt_setting;

    // Read PCM samples from TinyUSB FIFO
    int16_t temp_buf[64];
    uint16_t bytes_read;

    while ((bytes_read = tud_audio_read(temp_buf, sizeof(temp_buf))) > 0) {
        uint16_t samples = bytes_read / sizeof(int16_t);
        for (uint16_t i = 0; i < samples; i++) {
            spk_buf[spk_wr_idx & (SPK_BUF_SIZE - 1)] = temp_buf[i];
            spk_wr_idx++;
        }
    }

    return true;
}

bool mic_timer_callback(struct repeating_timer *t) {
    (void)t;
    if (!audio_active_mic) return true;

    adc_select_input(mic_adc_input);
    uint16_t raw = adc_read();

    // DC offset removal with slow IIR filter
    dc_offset = dc_offset - (dc_offset >> 4) + raw;
    int32_t dc = dc_offset >> 4;

    // Convert 12-bit to 16-bit signed
    int32_t sample = ((int32_t)raw - dc) * 16;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;

    if (mic_muted) sample = 0;

    mic_buf[mic_wr_idx & (MIC_BUF_SIZE - 1)] = (int16_t)sample;
    mic_wr_idx++;

    return true;
}
//--------------------------------------------------------------------
// Audio: Microphone - prepare data to send to host
// Called before TinyUSB needs to send a mic packet.
// We read the ADC and write PCM samples into the TinyUSB FIFO.
//--------------------------------------------------------------------
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in,
                                    uint8_t cur_alt_setting) {
    (void) rhport; (void) itf; (void) ep_in; (void) cur_alt_setting;

    if (!audio_active_mic) return true;

    uint16_t samples_per_frame = current_sample_rate / 1000;
    int16_t frame_buf[48];

    if (samples_per_frame > 48) samples_per_frame = 48;

    for (uint16_t i = 0; i < samples_per_frame; i++) {
        uint32_t available = mic_wr_idx - mic_rd_idx;
        if (available > 0) {
            frame_buf[i] = mic_buf[mic_rd_idx & (MIC_BUF_SIZE - 1)];
            mic_rd_idx++;
        } else {
            frame_buf[i] = 0; // silence if buffer underrun
        }
    }

    tud_audio_write((uint8_t *)frame_buf, samples_per_frame * sizeof(int16_t));
    return true;
}


//--------------------------------------------------------------------
// Audio: Speaker playback task
// Called from the main loop to push buffered audio to PWM output.
// This should be called frequently for smooth playback.
//--------------------------------------------------------------------
// Replace audio_spk_task with a timer callback
bool spk_timer_callback(struct repeating_timer *t) {
    (void)t;
    if (!audio_active_spk) return true;

    uint32_t available = spk_wr_idx - spk_rd_idx;
    if (available == 0) {
        pwm_set_gpio_level(spk_pwm_pin, 512); // silence at midpoint
        return true;
    }

    int16_t sample = spk_buf[spk_rd_idx & (SPK_BUF_SIZE - 1)];
    spk_rd_idx++;

    if (spk_muted) sample = 0;

    // Convert 16-bit signed to 10-bit unsigned for PWM
    // Shift from [-32768, 32767] to [0, 1023]
    uint16_t pwm_val = (uint16_t)((sample + 32768) >> 6);

    pwm_set_gpio_level(spk_pwm_pin, pwm_val);
    return true;
}




//--------------------------------------------------------------------
// Audio: Clock control requests
// The host asks us about our sample rate. We report what we support.
//--------------------------------------------------------------------
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    // Not used in our simple setup
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    // Not used in our simple setup
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;

    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

    //--- Clock Source requests ---//
    if (entityID == UAC2_ENTITY_CLOCK) {
        switch (ctrlSel) {
            case AUDIO_CS_CTRL_SAM_FREQ: {
                if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                    // Host wants the current sample rate
                    audio_control_cur_4_t curf = { .bCur = tu_htole32(current_sample_rate) };
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                        &curf, sizeof(curf));
                } else if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                    // Host wants to know what sample rates we support
                    audio_control_range_4_n_t(1) rangef = {
                        .wNumSubRanges = tu_htole16(1),
                        .subrange[0] = {
                            .bMin = tu_htole32(16000),
                            .bMax = tu_htole32(16000),
                            .bRes = tu_htole32(0)
                        }
                    };
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                        &rangef, sizeof(rangef));
                }
                break;
            }
            case AUDIO_CS_CTRL_CLK_VALID: {
                // Host asks if our clock is valid
                audio_control_cur_1_t cur_valid = { .bCur = 1 };
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                    &cur_valid, sizeof(cur_valid));
            }
        }
    }

    //--- Speaker Feature Unit requests ---//
    if (entityID == UAC2_ENTITY_SPK_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO_FU_CTRL_MUTE: {
                audio_control_cur_1_t mute = { .bCur = spk_muted };
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                    &mute, sizeof(mute));
            }
            case AUDIO_FU_CTRL_VOLUME: {
                if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                    audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(spk_volume) };
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                        &cur_vol, sizeof(cur_vol));
                } else if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                    // Report volume range: min, max, resolution (in 1/256 dB)
                    // Range: -60dB to 0dB in 1dB steps
                    audio_control_range_2_n_t(1) vol_range = {
                        .wNumSubRanges = tu_htole16(1),
                        .subrange[0] = {
                            .bMin = tu_htole16(-60 * 256),  // -60 dB
                            .bMax = tu_htole16(0),          //   0 dB
                            .bRes = tu_htole16(256)         //   1 dB steps
                        }
                    };
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request,
                        &vol_range, sizeof(vol_range));
                }
            }
        }
    }

    return false;
}


//--------------------------------------------------------------------
// Audio: Set requests from host (host changes volume/mute/sample rate)
//--------------------------------------------------------------------
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void) rhport; (void) pBuff;
    return true;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void) rhport; (void) pBuff;
    return true;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void) rhport;

    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

    //--- Clock Source ---//
    if (entityID == UAC2_ENTITY_CLOCK) {
        if (ctrlSel == AUDIO_CS_CTRL_SAM_FREQ) {
            // Host is setting the sample rate
            uint32_t new_rate = ((audio_control_cur_4_t const *)pBuff)->bCur;
            current_sample_rate = new_rate;
            return true;
        }
    }

    //--- Speaker Feature Unit ---//
    if (entityID == UAC2_ENTITY_SPK_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO_FU_CTRL_MUTE: {
                spk_muted = ((audio_control_cur_1_t const *)pBuff)->bCur;
                return true;
            }
            case AUDIO_FU_CTRL_VOLUME: {
                spk_volume = ((audio_control_cur_2_t const *)pBuff)->bCur;
                return true;
            }
        }
    }

    return false;
}


//--------------------------------------------------------------------
// Audio: Feedback callback
// Tells the host what our actual sample rate is so it can adjust
// how much data it sends us. For a simple setup with no external
// audio clock, we just report the nominal rate.
//--------------------------------------------------------------------
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf,
                                   audio_feedback_params_t* feedback_param) {
    (void) func_id;
    (void) alt_itf;

    // Use FIFO-based feedback: TinyUSB monitors the FIFO fill level
    // and automatically adjusts the feedback value
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = current_sample_rate;
}

//-------------------------------
//  USB HID
//-------------------------------

static void send_hid_report(uint8_t report_id) {


    //skip if HID isn't ready yet
    if(!tud_hid_ready()) {
        return;
    }

    switch(report_id){

        case REPORT_ID_KEYBOARD: {
            keyboard_task(report_id);
            break;
        }

        default: break;
    }
}

// Every 5ms, send 1 report for each HID profile
// tud_hid_report_complete_cb() is used to send the next report after the previouse one is complete
void hid_task(void) {
    const uint32_t interval_ms = 5;
    static uint32_t start_ms = 0;

    

    if (board_millis() - start_ms < interval_ms) {
        //Not enough time has passed
        return;
    }

    start_ms += interval_ms;

    send_hid_report(REPORT_ID_KEYBOARD);

}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {

  uint8_t next_report_id = report[0] + 1u;

  if (next_report_id < REPORT_ID_COUNT)
  {
    send_hid_report(next_report_id);
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {

  if (report_type == HID_REPORT_TYPE_OUTPUT) {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD){
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) {
        return;
      }

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      }else {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}



//---------------------------------------------------
//  Keyboard Task
//---------------------------------------------------

//10 max pulses + 1 to account for the invalid case of zero pulses
const uint8_t pulse_to_keycode[11] = {
    0,
    HID_KEY_1,
    HID_KEY_2,
    HID_KEY_3,
    HID_KEY_4,
    HID_KEY_5,
    HID_KEY_6,
    HID_KEY_7,
    HID_KEY_8,
    HID_KEY_9,
    HID_KEY_0
};

// Substitutes the basekey to the next apropriate key
uint8_t t9_substitute(uint8_t lastKeycode, uint8_t baseKey){

    switch (baseKey) {
        case HID_KEY_1: {
            if (lastKeycode == HID_KEY_PERIOD) return HID_KEY_SPACE;
            if (lastKeycode == HID_KEY_SPACE) return HID_KEY_SHIFT_LEFT;
            return HID_KEY_PERIOD;
        }
        case HID_KEY_2: {
            if (lastKeycode == HID_KEY_A) return HID_KEY_B;
            if (lastKeycode == HID_KEY_B) return HID_KEY_C;
            return HID_KEY_A;
        }
        case HID_KEY_3: {
            if (lastKeycode == HID_KEY_D) return HID_KEY_E;
            if (lastKeycode == HID_KEY_E) return HID_KEY_F;
            return HID_KEY_D;
        }
        case HID_KEY_4: {
            if (lastKeycode == HID_KEY_G) return HID_KEY_H;
            if (lastKeycode == HID_KEY_H) return HID_KEY_I;
            return HID_KEY_G;
        }
        case HID_KEY_5: {
            if (lastKeycode == HID_KEY_J) return HID_KEY_K;
            if (lastKeycode == HID_KEY_K) return HID_KEY_L;
            return HID_KEY_J;
        }
        case HID_KEY_6: {
            if (lastKeycode == HID_KEY_M) return HID_KEY_N;
            if (lastKeycode == HID_KEY_N) return HID_KEY_O;
            return HID_KEY_M;
        }
        case HID_KEY_7: {//My dial doesn't have a Q so I added it here. That also makes this a 4 key longcycle
            if (lastKeycode == HID_KEY_P) return HID_KEY_Q;
            if (lastKeycode == HID_KEY_Q) return HID_KEY_R;
            if (lastKeycode == HID_KEY_R) return HID_KEY_S;
            return HID_KEY_P;
        }
        case HID_KEY_8: {
            if (lastKeycode == HID_KEY_T) return HID_KEY_U;
            if (lastKeycode == HID_KEY_U) return HID_KEY_V;
            return HID_KEY_T;
        }
        case HID_KEY_9: {
            if (lastKeycode == HID_KEY_W) return HID_KEY_X;
            if (lastKeycode == HID_KEY_X) return HID_KEY_Y;
            if (lastKeycode == HID_KEY_Y) return HID_KEY_Z;
            return HID_KEY_W;
        }
        
        default: {
            return baseKey;
        }
    }
}

// Checks if the last key sent is in the T9 cycle for the baseKey
bool t9_validate_last_key(uint8_t lastKeyCode, uint8_t baseKey){

    switch (baseKey) {
        case HID_KEY_1: {
            return (lastKeyCode == baseKey || (lastKeyCode == HID_KEY_PERIOD || lastKeyCode == HID_KEY_SPACE || lastKeyCode == HID_KEY_SHIFT_LEFT));
        }
        case HID_KEY_2: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_A && lastKeyCode <= HID_KEY_C));
        }
        case HID_KEY_3: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_D && lastKeyCode <= HID_KEY_F));
        }
        case HID_KEY_4: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_G && lastKeyCode <= HID_KEY_I));
        }
        case HID_KEY_5: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_J && lastKeyCode <= HID_KEY_L));
        }
        case HID_KEY_6: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_M && lastKeyCode <= HID_KEY_N));
        }
        case HID_KEY_7: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_P && lastKeyCode <= HID_KEY_S));
        }
        case HID_KEY_8: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_T && lastKeyCode <= HID_KEY_V));
        }
        case HID_KEY_9: {
            return (lastKeyCode == baseKey || (lastKeyCode >= HID_KEY_W && lastKeyCode <= HID_KEY_Z));
        }
        default: {
            return false;
        }
    }
}

void keyboard_task(uint8_t report_id){

    //Cooldown to go to next number rather than switching to next letter
    const uint32_t t9Cooldown = 1000;

    // Avoid sending multiple consecutive zero reports for keyboard
    static bool hasKeyboardKey = false;
    
    //Save the last state of the pulse pin so we can count the number of pulses
    static bool lastPulseState;

    //Save the last state of the signal pin so we know when we start a new character
    static bool lastSignalState;

    //save the number of pulses
    static uint8_t pulseCount = 0;

    //For T9 typing
    static int lastKeycode = 0;

    //T9 cooldown
    static uint32_t lastKeyTime = 0;

    //Used to get the state of necessary pins the first time the function is called.
    static bool initialized = false;
    if(!initialized){
        lastPulseState = gpio_get(pulse_pin);
        lastSignalState = gpio_get(signal_pin);
        initialized = true;
    }

    //Should T9 be enabled?
    bool tryT9 = (board_millis() - lastKeyTime < t9Cooldown);


    if(gpio_get(signal_pin)){

        //If tryT9 is true at the start of a dial, keep setting it to prevent it from changing. We care about when the dial is started.
        if(tryT9){
            lastKeyTime = board_millis();
        }

        //Pulse counting
        if(gpio_get(pulse_pin) && !lastPulseState) {
            pulseCount += 1;
        }

        lastPulseState = gpio_get(pulse_pin);
        lastSignalState = true;
    } else {
        //Set lastKeyTime right after we finish dialing
        if(lastSignalState){
            lastKeyTime = board_millis();
            lastSignalState = false;
        }
        if(pulseCount > 0){

            uint8_t keycode[6] = {0};
            
            //It is possible to wiggle the dial and get extra pulses
            if(pulseCount <= 10){
                keycode[0] = pulse_to_keycode[pulseCount];

                //TODO Optimize and make cleaner, this could 100% be simplified with more flexible functionality
                if (tryT9 && keycode[0] == HID_KEY_1 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_1);
                }
                if (tryT9 && keycode[0] == HID_KEY_2 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_2);
                }
                if (tryT9 && keycode[0] == HID_KEY_3 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_3);
                }
                if (tryT9 && keycode[0] == HID_KEY_4 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_4);
                }
                if (tryT9 && keycode[0] == HID_KEY_5 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_5);
                }
                if (tryT9 && keycode[0] == HID_KEY_6 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_6);
                }
                if (tryT9 && keycode[0] == HID_KEY_7 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_7);
                }
                if (tryT9 && keycode[0] == HID_KEY_8 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_8);
                }
                if (tryT9 && keycode[0] == HID_KEY_9 && t9_validate_last_key(lastKeycode, keycode[0])) {
                    keycode[0] = HID_KEY_BACKSPACE;
                    keycode[1] = t9_substitute(lastKeycode, HID_KEY_9);
                }

            }
            
            pulseCount = 0;

            if(keycode[0] == HID_KEY_BACKSPACE){
                lastKeycode = keycode[1];
            } else {
                lastKeycode = keycode[0];
            }
            tud_hid_keyboard_report(report_id, 0 ,keycode);
            hasKeyboardKey = true;

            } else {
            
            
            //Send empty key report to stop endless keyyyyypreeeesssssesssssssssssssssssssssssssssss (you get the idea)
            if (hasKeyboardKey) {
                tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
            }
            hasKeyboardKey = false;
        
        }
    }
}


//------------------------------------
//  Blinking Task
//------------------------------------
void led_blinking_task(void)

{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}