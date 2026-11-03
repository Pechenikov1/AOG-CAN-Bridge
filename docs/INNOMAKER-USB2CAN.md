# InnoMaker USB2CAN-C backend notes

This document records the integration boundary for the USB2CAN-C backend. It is an implementation plan, not a claim that the adapter has been tested.

## Existing bridge boundary

The broker currently calls PCAN-Basic directly from `BridgeForm`: it initializes PCAN-USB at 250 kbit/s, polls `CAN_Read`, sends frames with `CAN_Write`, and uninitializes the channel. The VT/Task Controller proxy and its loopback UDP packet protocol do not need to know which physical CAN adapter is used.

The intended change is to put a physical-adapter interface behind the broker while leaving the PCAN proxy ABI and UDP protocol untouched. The first implementation milestone is USB2CAN-C only; PCAN remains available as the existing/default backend.

## Confirmed upstream InnoMaker API

The official Windows C++ SDK header in `INNO-MAKER/usb2can` exposes these operations on `InnoMakerUsb2CanLib`:

- `setup()` / `setdown()` for SDK lifetime.
- `scanInnoMakerDevice()`, `getInnoMakerDeviceCount()`, `getInnoMakerDevice(index)` for discovery.
- `urbSetupDevice(device, mode, bittiming)` to configure/open CAN.
- `sendInnoMakerDeviceBuf(device, bytes, size, timeout)` and `recvInnoMakerDeviceBuf(device, bytes, size, timeout)` for frames.
- `urbResetDevice(device)` and `closeInnoMakerDevice(device)` for shutdown.

The vendor's `innomaker_host_frame` is a Classic CAN frame with `echo_id`, `can_id`, `can_dlc`, channel/flags/reserved, 8 data bytes and a microsecond timestamp. The sample identifies received bus frames with `echo_id == 0xffffffff`; other echo IDs are transmit completions and must not be forwarded as received bus traffic. The backend must handle that distinction and avoid leaking the SDK's transmit contexts.

For this bridge, the intended CAN settings are normal mode, 250 kbit/s, Classic CAN. The vendor C# sample's 250K timing tuple is `prop_seg=6`, `phase_seg1=7`, `phase_seg2=2`, `sjw=1`, `brp=12`; verify this against the exact USB2CAN-C SDK/firmware and a bench before using it on a live bus.

## Integration approach

The SDK is a C++ class library, not a PCAN-Basic DLL and not a C ABI. Calling it directly from the existing C# broker with ordinary P/Invoke is not safe. The proposed boundary is a small native C ABI wrapper around the vendor SDK, called by a C# `ICanAdapter` implementation. It will:

1. own one SDK instance and one selected device;
2. expose open, receive, send, and close operations using plain fixed-width frame fields;
3. translate standard/extended ID flags and DLC/data without changing the existing broker packet format;
4. distinguish receive frames from TX echo/completion records;
5. return explicit status/error codes and close/reset the device reliably.

The wrapper must be built for x64 with the matching vendor x64 import library/runtime DLL and any required libusb runtime. Vendor binaries should not be copied into this repository until their redistribution terms are confirmed. The official SDK remains the source of truth for structure layout and dependency requirements.

## Validation gates

- Build the C# broker and native wrapper for x64.
- Test device discovery, 250 kbit/s configuration, open/close/reopen, standard and extended frames, and transmit echo handling on a disconnected/isolated bench.
- Test VT and Task Controller together with the existing proxy and confirm bidirectional forwarding.
- Only after those checks consider a carefully supervised machine test. This experimental software is not safety-rated.

## User-side items needed later

No upload is needed to begin source work. For hardware validation, the tester will need the USB2CAN-C connected, the official InnoMaker Windows driver/runtime installed as required by the vendor SDK, and a safe bench CAN network with correct termination. We will request logs or SDK-version details only if a concrete build/runtime issue requires them.
