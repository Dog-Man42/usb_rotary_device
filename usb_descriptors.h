/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ha Thach (tinyusb.org)
 * Copyright (c) 2020 Jerzy Kasenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

enum
{
  REPORT_ID_KEYBOARD = 1,
  REPORT_ID_CONSUMER_CONTROL,
  REPORT_ID_COUNT
};

//--------------------------------------------------------------------
// UAC2 Entity IDs (arbitrary but must be unique within audio function)
//
// These identify the "units" inside the audio function's topology.
// Think of it like a signal flow diagram:
//
// SPEAKER PATH (host -> device):
//   USB OUT -> [Input Terminal] -> [Feature Unit (volume)] -> [Output Terminal] -> speaker
//
// MICROPHONE PATH (device -> host):
//   microphone -> [Input Terminal] -> [Output Terminal] -> USB IN
//
// Both paths share one Clock Source.
//--------------------------------------------------------------------
#define UAC2_ENTITY_CLOCK                   0x04

// Speaker path entities
#define UAC2_ENTITY_SPK_INPUT_TERMINAL      0x01
#define UAC2_ENTITY_SPK_FEATURE_UNIT        0x02
#define UAC2_ENTITY_SPK_OUTPUT_TERMINAL     0x03

// Microphone path entities (use different IDs from speaker path!)
#define UAC2_ENTITY_MIC_INPUT_TERMINAL      0x11
#define UAC2_ENTITY_MIC_OUTPUT_TERMINAL     0x13

//--------------------------------------------------------------------
// Interface Numbers
//
// The composite device exposes these interfaces to the host:
//   0: Audio Control      (manages clock, volume, mute)
//   1: Audio Streaming SPK (isochronous OUT - host sends audio)
//   2: Audio Streaming MIC (isochronous IN  - device sends audio)
//   3: HID                (your keyboard)
//--------------------------------------------------------------------
enum
{
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING_SPK,
  ITF_NUM_AUDIO_STREAMING_MIC,
  ITF_NUM_HID,
  ITF_NUM_TOTAL
};

//--------------------------------------------------------------------
// Endpoint Addresses
//
// RP2040 has 16 endpoints (0-15), each with IN and OUT directions.
// EP0 is reserved for control. We need:
//   - Audio OUT (speaker):   EP 0x01 (OUT)
//   - Audio IN  (mic):       EP 0x81 (IN)
//   - Audio Feedback:        EP 0x82 (IN)  -- feedback for speaker
//   - HID IN:                EP 0x83 (IN)
//--------------------------------------------------------------------
#define EPNUM_AUDIO_OUT     0x01
#define EPNUM_AUDIO_IN      0x81
#define EPNUM_AUDIO_FB      0x82
#define EPNUM_HID           0x83

#endif /* USB_DESCRIPTORS_H_ */
