# CLAUDE.md

The AllSense Arduino library (library at the repo root, as the Arduino Library Manager requires): `AllSenseSerial` (any board, compact protocol over a USB cable to the phone), `AllSenseBLE` (ESP32, compact protocol over BLE), `AllSense` (ESP32, JSON over Wi-Fi), `AllSenseCompact.h` (framing). Examples in `examples/` (the docs site includes them, so they stay in the compile matrix); host tests in `extras/test/`; `extras/tools/board.py` resets a board and watches it reconnect. Public, MIT.

## Working here

- **The user makes every commit and push.** Edit files and run the tests, then hand over: what changed, the test results, and a one-line commit message suggestion. Never run `git commit`, `git push`, tags or releases (Library Manager submissions included); add no Co-Authored-By line unless asked.
- **Public repository:** never write the user's serials, IPs, Wi-Fi details, home location, tokens, file paths or ports into it. Machine facts go in `CLAUDE.local.md` (git-ignored).
- **Releases:** the Library Manager picks up a release from a git tag matching `version=` in `library.properties`; bump it for every release.

## Test

- Host tests (g++, Linux or WSL): `bash extras/test/run.sh`.
- Compile matrix (arduino-cli): ESP32-S3 (`esp32:esp32:esp32s3`): WiFiQuickstart, USBQuickstart, USBMinimal, BLEQuickstart, TemplateReader, TemplateReaderBLE; Uno (`arduino:avr:uno`) and Pico (`arduino:mbed_rp2040:pico`): USBQuickstart, USBMinimal, TemplateReader. Compile with `--library <this repo>` so the build uses this copy, not an installed one. CI runs all of it plus arduino-lint.

## How it fits

- Speaks the compact protocol and JSON protocol from `otterbridge-allsense-protocol`; `extras/test/test_compact.cpp` must agree with its `vectors/compact_vectors.json`. A protocol change lands there first, then here.
- On a PC, the test kit's fake phones stand in for the phone: `allsense-fake-serial`, `allsense-fake-ble`, `allsense-fake-template` (`pip install "allsense-testkit[all]"`).
- The Wi-Fi and BLE classes are guarded to ESP32; `AllSenseSerial` must keep compiling for Uno (2 KB RAM). USB to an ESP32: DTR on, RTS off.
