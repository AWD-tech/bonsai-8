Zephyr `subsys/usb/device_next/class` files as this firmware actually
runs them, carrying TWO local changes that live outside this repo's
normal tree:

1. The UAC2 Windows enumeration backport (`USBD_UAC2_FS_WINDOWS_WORKAROUND`).
2. U1 / SOF-OFF (`SP1_U1_SOF_OFF`): `CONFIG_UDC_ENABLE_SOF=n`; the
   explicit-feedback regulator is re-armed from the ISO OUT completion
   instead of the per-frame SOF interrupt (~530 us/period back).

To reproduce: copy these three files over a matching Zephyr checkout.
Captured 2026-08-26 from the tree that built `sp1_looper_P14S-TWOTIER.bin`.
