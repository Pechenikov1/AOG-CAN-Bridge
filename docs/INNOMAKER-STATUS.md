## InnoMaker USB2CAN-C work

Development is in progress on `feature/innomaker-usb2can`. The proposed implementation adds a physical-adapter backend behind the existing broker; the PCAN-compatible client proxy and UDP protocol remain unchanged. The official SDK is a C++ class API, so integration requires a small native C ABI wrapper rather than directly loading the vendor DLL through the PCAN imports.

See [INNOMAKER-USB2CAN.md](INNOMAKER-USB2CAN.md) for the confirmed API surface, 250 kbit/s configuration target, dependency notes, and staged bench-validation plan. The native wrapper source is being prepared, but this backend is not yet wired into BridgeForm and hardware communication is not verified. Do not use the prototype for safety-critical control or connect it to machinery before bench validation.
