#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <InnoMakerUsb2CanLib.h>

#ifdef _WIN32
#define IM_EXPORT extern "C" __declspec(dllexport)
#else
#define IM_EXPORT extern "C"
#endif

// Stable C ABI consumed by the C# broker. Keep this independent of vendor C++
// classes and verify the frame size/layout when updating the SDK.
#pragma pack(push, 4)
struct IM_CAN_FRAME
{
    std::uint32_t identifier;
    std::uint32_t flags; // bit 0 = extended
    std::uint8_t dlc;
    std::uint8_t data[8];
    std::uint32_t timestamp_us;
};
#pragma pack(pop)

struct IM_HANDLE
{
    InnoMakerUsb2CanLib* sdk = nullptr;
    InnoMakerUsb2CanLib::InnoMakerDevice* device = nullptr;
    InnoMakerUsb2CanLib::innomaker_can tx_state{};
    bool opened = false;
};

static constexpr int IM_OK = 0;
static constexpr int IM_TIMEOUT = 1;
static constexpr int IM_INVALID_ARGUMENT = -1;
static constexpr int IM_DEVICE_ERROR = -2;
static constexpr int IM_UNSUPPORTED_BITRATE = -3;
static constexpr std::uint32_t CAN_EFF_FLAG = 0x80000000u;
static constexpr std::uint32_t CAN_RTR_FLAG = 0x40000000u;
static constexpr std::uint32_t CAN_ERR_FLAG = 0x20000000u;
static constexpr std::uint32_t CAN_ID_MASK = 0x1FFFFFFFu;
static constexpr std::uint32_t RX_ECHO_ID = 0xFFFFFFFFu;

IM_EXPORT int IM_CALL IM_Open(std::uint32_t device_index, std::uint32_t bitrate,
    IM_HANDLE** output)
{
    if (!output) return IM_INVALID_ARGUMENT;
    *output = nullptr;
    if (bitrate != 250000u) return IM_UNSUPPORTED_BITRATE;

    std::unique_ptr<IM_HANDLE> handle(new (std::nothrow) IM_HANDLE());
    if (!handle) return IM_DEVICE_ERROR;
    handle->sdk = new (std::nothrow) InnoMakerUsb2CanLib();
    if (!handle->sdk || !handle->sdk->setup()) return IM_DEVICE_ERROR;

    if (!handle->sdk->scanInnoMakerDevice() ||
        device_index >= static_cast<std::uint32_t>(handle->sdk->getInnoMakerDeviceCount()))
    {
        handle->sdk->setdown();
        return IM_DEVICE_ERROR;
    }

    handle->device = handle->sdk->getInnoMakerDevice(static_cast<int>(device_index));
    if (!handle->device) {
        handle->sdk->setdown();
        return IM_DEVICE_ERROR;
    }

    InnoMakerUsb2CanLib::Innomaker_device_bittming timing{};
    // 250 kbit/s tuple from the vendor's official C# USB2CAN-C sample.
    timing.prop_seg = 6;
    timing.phase_seg1 = 7;
    timing.phase_seg2 = 2;
    timing.sjw = 1;
    timing.brp = 12;

    if (!handle->sdk->urbSetupDevice(handle->device,
        InnoMakerUsb2CanLib::UsbCanModeNormal, timing))
    {
        handle->sdk->closeInnoMakerDevice(handle->device);
        handle->sdk->setdown();
        return IM_DEVICE_ERROR;
    }

    for (int i = 0; i < handle->sdk->innomaker_MAX_TX_URBS; ++i)
        handle->tx_state.tx_context[i].echo_id =
            static_cast<std::uint32_t>(handle->sdk->innomaker_MAX_TX_URBS);

    handle->opened = true;
    *output = handle.release();
    return IM_OK;
}

IM_EXPORT int IM_CALL IM_Send(IM_HANDLE* handle, const IM_CAN_FRAME* input)
{
    if (!handle || !handle->opened || !input || input->dlc > 8 ||
        (input->flags & ~1u) != 0)
        return IM_INVALID_ARGUMENT;

    const std::uint32_t max_id = (input->flags & 1u) ? 0x1FFFFFFFu : 0x7FFu;
    if (input->identifier > max_id) return IM_INVALID_ARGUMENT;

    auto* context = handle->sdk->innomaker_alloc_tx_context(&handle->tx_state);
    if (!context || context->echo_id == 0xFFu) return IM_DEVICE_ERROR;

    InnoMakerUsb2CanLib::innomaker_host_frame frame{};
    frame.echo_id = context->echo_id;
    frame.can_id = input->identifier | ((input->flags & 1u) ? CAN_EFF_FLAG : 0u);
    frame.can_dlc = input->dlc;
    frame.channel = 0;
    frame.flags = 0;
    frame.reserved = 0;
    std::memcpy(frame.data, input->data, 8);
    frame.timestamp_us = 0;

    const bool sent = handle->sdk->sendInnoMakerDeviceBuf(handle->device,
        reinterpret_cast<BYTE*>(&frame), sizeof(frame), 10);
    if (!sent) {
        auto* allocated = handle->sdk->innomaker_get_tx_context(
            &handle->tx_state, context->echo_id);
        if (allocated) handle->sdk->innomaker_free_tx_context(allocated);
        return IM_DEVICE_ERROR;
    }
    return IM_OK;
}

// Returns IM_OK for a bus frame, IM_TIMEOUT when no frame arrived by timeout,
// and a negative code on SDK/argument errors. TX-completion echoes are consumed
// and freed here, never exposed as incoming bus frames.
IM_EXPORT int IM_CALL IM_Receive(IM_HANDLE* handle, IM_CAN_FRAME* output,
    std::uint32_t timeout_ms)
{
    if (!handle || !handle->opened || !output) return IM_INVALID_ARGUMENT;
    const DWORD started = GetTickCount();
    do {
        InnoMakerUsb2CanLib::innomaker_host_frame frame{};
        const bool received = handle->sdk->recvInnoMakerDeviceBuf(handle->device,
            reinterpret_cast<BYTE*>(&frame), sizeof(frame), 10);
        if (!received) {
            if (GetTickCount() - started >= timeout_ms) return IM_TIMEOUT;
            continue;
        }

        if (frame.echo_id != RX_ECHO_ID) {
            auto* context = handle->sdk->innomaker_get_tx_context(
                &handle->tx_state, frame.echo_id);
            if (context) handle->sdk->innomaker_free_tx_context(context);
            if (GetTickCount() - started >= timeout_ms) return IM_TIMEOUT;
            continue;
        }

        if (frame.can_dlc > 8 || (frame.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)))
            continue; // current bridge protocol carries Classic CAN data frames only

        const bool extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        output->identifier = frame.can_id & CAN_ID_MASK;
        output->flags = extended ? 1u : 0u;
        output->dlc = frame.can_dlc;
        std::memcpy(output->data, frame.data, 8);
        output->timestamp_us = frame.timestamp_us;
        return IM_OK;
    } while (GetTickCount() - started < timeout_ms);
    return IM_TIMEOUT;
}

IM_EXPORT int IM_CALL IM_Close(IM_HANDLE* handle)
{
    if (!handle) return IM_OK;
    int result = IM_OK;
    if (handle->opened && handle->sdk && handle->device) {
        if (!handle->sdk->urbResetDevice(handle->device)) result = IM_DEVICE_ERROR;
        if (!handle->sdk->closeInnoMakerDevice(handle->device)) result = IM_DEVICE_ERROR;
        handle->opened = false;
    }
    if (handle->sdk) {
        if (!handle->sdk->setdown()) result = IM_DEVICE_ERROR;
        delete handle->sdk;
    }
    delete handle;
    return result;
}
