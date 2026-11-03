## InnoMaker USB2CAN-C work

Development is in progress on `feature/innomaker-usb2can`. The proposed implementation adds a physical-adapter backend behind the existing broker; the PCAN-compatible client proxy and UDP protocol remain unchanged. The official C++ SDK is a C++ class API, so the broker will need a small native C ABI wrapper rather than directly loading the vendor DLL through the existing PCAN declarations.

See [docs/INNOMAKER-USB2CAN.md](docs/INNOMAKER-USB2CAN.md) for the confirmed API surface, 250 kbit/s configuration target, dependency notes, and staged bench-validation plan. Hardware communication is not yet implemented or verified. Do not use the prototype for safety-critical control or connect it to machinery before bench validation.
