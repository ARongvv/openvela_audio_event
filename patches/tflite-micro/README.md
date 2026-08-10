# TFLite Micro ESP-NN integration patch

`0001-add-esp-nn-backend.patch` is the complete OpenVela framework delta for
the ESP-NN adapter kept in `ccf_audioevent/third_party/tflm-espnn-adapter/`.
It changes only these files in the OpenVela workspace:

- `apps/mlearning/tflite-micro/Kconfig`;
- `apps/mlearning/tflite-micro/Makefile`;
- `apps/mlearning/tflite-micro/CMakeLists.txt`.

The patch adds the ESP-NN Kconfig controls, the ESP32-S3 CCOUNT time source,
and equivalent Make/CMake source-selection rules.  It expects the adapter
directory at `apps/mlearning/tflite-micro/kernels/esp_nn`; that directory can
remain a direct copy during transition or later become a symbolic link to this
project's adapter source.

From the OpenVela workspace root, validate a clean upstream TFLM tree before
applying it:

```bash
patch --dry-run -p1 < ccf_audioevent/patches/tflite-micro/0001-add-esp-nn-backend.patch
patch -p1 < ccf_audioevent/patches/tflite-micro/0001-add-esp-nn-backend.patch
```

For a workspace which already contains the integration, use the reverse dry
run to verify that the patch describes the installed delta without changing
anything:

```bash
patch --dry-run -R -p1 < ccf_audioevent/patches/tflite-micro/0001-add-esp-nn-backend.patch
```

Do not apply this patch blindly if any of the three target files contain local
unrelated edits.  Resolve those edits first, then regenerate or rebase the
patch from the new OpenVela baseline.
