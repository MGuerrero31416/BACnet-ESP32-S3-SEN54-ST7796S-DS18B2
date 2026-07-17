## Critical change-scope rules

- Make the smallest change required; do not refactor unrelated code.
- Do not delete, reduce, renumber, or disable BACnet objects unless explicitly requested.
- Removing a sensor assignment from an AV does not authorize removing the AV.
- Preserve unaffected object counts, mappings, NVS keys, tasks, GPIOs, and diagnostics.
- Do not switch branches, restore files, rename folders, or copy code from another project.
- Fix only errors caused by the requested change; report unrelated errors without modifying them.
- Do not flash, erase NVS, run fullclean, or delete the build folder unless explicitly requested.

## Mandatory build environment

Build only with:

- ESP-IDF v5.5.4
- `C:\esp\v5.5.4\esp-idf`
- Target `esp32s3`
- Arduino-ESP32 3.3.10

Never run `idf.py`, CMake, or Ninja directly. Build only with:

`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build_idf55.ps1 build`

If the build environment is incorrect, stop and report it. Do not change source code,
dependencies, CMake files, version constraints, or the target to make the build proceed.