# usb_rotary_device

Converts a Western Electric rotary phone into a Raspberry Pi Pico composite USB device.
Acts as a keyboard controlled by the dial, and a headset controlled by the original
microphone and speaker.

## Third-party code

`tusb_config.h` and `usb_descriptors.c` are adapted from the TinyUSB `uac2_headset`
example — copyright Ha Thach (tinyusb.org), MIT licensed.

## License

MIT — see [LICENSE](LICENSE).
