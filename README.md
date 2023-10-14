#handleit-embedded

To build for the nrf52840 dongle uncomment the UART lines in [prj.conf](hub/prj.conf#L10)

The [merged.hex](hub/build_dongle/zephyr/merged.hex) file contains the full application and can be flashed directly to the dongle.

The [app_update.bin](hub/build_dongle/zephyr/app_update.bin) file contains just the update that can be flashed during a DFU update.

Use the antenna with the black side facing up.