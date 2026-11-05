#include "InnoMakerNativeBridge.h"
#include "InnoMakerUsb2CanLib.h"

#include <condition_variable>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <type_traits>

namespace
{
    std::deque<IM_CAN_FRAME> g_receiveQueue;
    std::mutex g_queueMutex;
    std::condition_variable g_queueChanged;
    bool g_mockMode = false;
    bool g_open = false;
    thread_local std::string g_lastError;

    InnoMakerUsb2CanLib g_sdk;
    InnoMakerUsb2CanLib::InnoMakerDevice* g_device = nullptr;
    bool g_realMode = false;
    BYTE g_rxBuffer[24]{};

    void SetError(const char* message)
    {
        g_lastError = message ? message : "";
    }
}

extern "C" IM_API int IM_Open(const char* devicePath, uint32_t baudrate)
{
    try
    {
        if (devicePath == nullptr || baudrate == 0)
        {
            SetError("Invalid device path or baud rate.");
            return IM_ERR_PARAM;
        }

        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_receiveQueue.clear();
        g_mockMode = std::strcmp(devicePath, "mock") == 0;
        g_open = false;
        g_realMode = false;
        g_device = nullptr;

        if (g_mockMode)
        {
            g_open = true;
            SetError("");
            return IM_OK;
        }

        if (baudrate != 250000)
        {
            SetError("Only 250 kbit/s is supported by this SDK configuration.");
            return IM_ERR_PARAM;
        }

        if (!g_sdk.setup())
        {
            SetError("InnoMakerUsb2CanLib::setup failed.");
            return IM_ERR_OPEN;
        }
        if (!g_sdk.scanInnoMakerDevice())
        {
            g_sdk.setdown();
            SetError("InnoMakerUsb2CanLib::scanInnoMakerDevice failed.");
            return IM_ERR_OPEN;
        }
        if (g_sdk.getInnoMakerDeviceCount() == 0)
        {
            g_sdk.setdown();
            SetError("No InnoMaker device was found.");
            return IM_ERR_OPEN;
        }

        g_device = g_sdk.getInnoMakerDevice(0);
        if (g_device == nullptr)
        {
            g_sdk.setdown();
            SetError("getInnoMakerDevice returned null.");
            return IM_ERR_OPEN;
        }
        if (!g_sdk.openInnoMakerDevice(g_device))
        {
            g_device = nullptr;
            g_sdk.setdown();
            SetError("InnoMakerUsb2CanLib::openInnoMakerDevice failed.");
            return IM_ERR_OPEN;
        }

        InnoMakerUsb2CanLib::Innomaker_device_bittming timing = { 6, 7, 2, 1, 12 };
        if (!g_sdk.urbSetupDevice(g_device,
            InnoMakerUsb2CanLib::UsbCanMode::UsbCanModeNormal, timing))
        {
            g_sdk.closeInnoMakerDevice(g_device);
            g_device = nullptr;
            g_sdk.setdown();
            SetError("InnoMakerUsb2CanLib::urbSetupDevice failed.");
            return IM_ERR_OPEN;
        }

        g_realMode = true;
        g_open = true;
        SetError("");
        return IM_OK;
    }
    catch (...)
    {
        try
        {
            if (g_device != nullptr)
                g_sdk.closeInnoMakerDevice(g_device);
            g_device = nullptr;
            g_sdk.setdown();
        }
        catch (...)
        {
        }
        g_realMode = false;
        g_open = false;
        SetError("Unexpected exception in IM_Open.");
        return IM_ERR_IO;
    }
}

extern "C" IM_API int IM_Send(const IM_CAN_FRAME* frame)
{
    try
    {
        if (frame == nullptr || frame->dlc > sizeof(frame->data))
        {
            SetError("Invalid CAN frame.");
            return IM_ERR_PARAM;
        }

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (!g_open)
            {
                SetError("Adapter is not open.");
                return IM_ERR_OPEN;
            }
            if (g_realMode)
            {
                InnoMakerUsb2CanLib::innomaker_host_frame sdkFrame = {};
                sdkFrame.can_id = frame->id;
                sdkFrame.can_dlc = frame->dlc;
                sdkFrame.channel = 0;
                sdkFrame.flags = 0;
                if (frame->flags & IM_FLAG_EXTENDED)
                    sdkFrame.can_id |= 0x80000000u;
                if (frame->flags & IM_FLAG_RTR)
                    sdkFrame.can_id |= 0x40000000u;
                std::memcpy(sdkFrame.data, frame->data, frame->dlc);
                // Sentinel requests no TX echo/confirmation frame from the SDK.
                sdkFrame.echo_id = 0xFFFFFFFFu;

                if (!g_sdk.sendInnoMakerDeviceBuf(g_device,
                    reinterpret_cast<BYTE*>(&sdkFrame), sizeof(sdkFrame), 1000))
                {
                    SetError("InnoMakerUsb2CanLib::sendInnoMakerDeviceBuf failed.");
                    return IM_ERR_IO;
                }
                SetError("");
                return IM_OK;
            }
            if (!g_mockMode)
            {
                SetError("Adapter is not open in mock or real mode.");
                return IM_ERR_OPEN;
            }

            IM_CAN_FRAME echo = *frame;
            echo.flags = static_cast<uint8_t>(echo.flags | IM_FLAG_TX_ECHO);
            g_receiveQueue.push_back(echo);
        }
        g_queueChanged.notify_one();
        SetError("");
        return IM_OK;
    }
    catch (...)
    {
        SetError("Unexpected exception in IM_Send.");
        return IM_ERR_IO;
    }
}

extern "C" IM_API int IM_Receive(IM_CAN_FRAME* frame, int timeoutMs)
{
    try
    {
        if (frame == nullptr || timeoutMs < 0)
        {
            SetError("Invalid receive arguments.");
            return IM_ERR_PARAM;
        }

        std::unique_lock<std::mutex> lock(g_queueMutex);
        if (!g_open)
        {
            SetError("Adapter is not open.");
            return IM_ERR_OPEN;
        }
        if (g_realMode)
        {
            struct InnoMakerHostFrame
            {
                uint32_t echo_id;
                uint32_t can_id;
                uint8_t can_dlc;
                uint8_t channel;
                uint8_t flags;
                uint8_t reserved;
                uint8_t data[8];
                uint32_t timestamp_us;
            };
            static_assert(sizeof(InnoMakerHostFrame) == 24,
                "InnoMaker host frame ABI must be 24 bytes");

            const unsigned int sdkTimeoutMs = static_cast<unsigned int>(timeoutMs < 10 ? 10 : timeoutMs);
            if (!g_sdk.recvInnoMakerDeviceBuf(g_device, g_rxBuffer,
                sizeof(g_rxBuffer), sdkTimeoutMs))
            {
                SetError("InnoMakerUsb2CanLib::recvInnoMakerDeviceBuf timed out or failed.");
                return IM_ERR_TIMEOUT;
            }

            InnoMakerHostFrame sdkFrame{};
            std::memcpy(&sdkFrame, g_rxBuffer, sizeof(sdkFrame));
            frame->id = sdkFrame.can_id & 0x1FFFFFFFu;
            frame->dlc = sdkFrame.can_dlc;
            frame->flags = 0;
            if (sdkFrame.can_id & 0x80000000u)
                frame->flags = static_cast<uint8_t>(frame->flags | IM_FLAG_EXTENDED);
            if (sdkFrame.can_id & 0x40000000u)
                frame->flags = static_cast<uint8_t>(frame->flags | IM_FLAG_RTR);
            std::memcpy(frame->data, sdkFrame.data, sizeof(frame->data));
            SetError("");
            return IM_OK;
        }
        if (!g_mockMode)
        {
            SetError("Adapter is not open in mock or real mode.");
            return IM_ERR_OPEN;
        }

        const bool available = g_queueChanged.wait_for(lock,
            std::chrono::milliseconds(timeoutMs),
            [] { return !g_receiveQueue.empty() || !g_open; });
        if (!available)
        {
            SetError("Receive timed out.");
            return IM_ERR_TIMEOUT;
        }
        if (!g_open)
        {
            SetError("Adapter was closed while waiting for a frame.");
            return IM_ERR_OPEN;
        }

        *frame = g_receiveQueue.front();
        g_receiveQueue.pop_front();
        lock.unlock();
        SetError("");
        return IM_OK;
    }
    catch (...)
    {
        SetError("Unexpected exception in IM_Receive.");
        return IM_ERR_IO;
    }
}

extern "C" IM_API void IM_Close(void)
{
    try
    {
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (g_realMode)
            {
                if (g_device != nullptr)
                    g_sdk.closeInnoMakerDevice(g_device);
                g_sdk.setdown();
                g_device = nullptr;
                g_realMode = false;
            }
            g_open = false;
            g_mockMode = false;
            g_receiveQueue.clear();
        }
        g_queueChanged.notify_all();
        SetError("");
    }
    catch (...)
    {
        g_device = nullptr;
        g_realMode = false;
        g_open = false;
        SetError("Unexpected exception in IM_Close.");
    }
}

extern "C" IM_API const char* IM_LastError(void)
{
    try
    {
        return g_lastError.c_str();
    }
    catch (...)
    {
        return "Unexpected exception in IM_LastError.";
    }
}

extern "C" IM_API int IM_InjectRx(const IM_CAN_FRAME* frame)
{
    try
    {
        if (frame == nullptr || frame->dlc > sizeof(frame->data))
        {
            SetError("Invalid injected CAN frame.");
            return IM_ERR_PARAM;
        }

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (g_realMode || !g_open || !g_mockMode)
            {
                SetError("RX injection is only available in open mock mode.");
                return IM_ERR_PARAM;
            }
            IM_CAN_FRAME received = *frame;
            received.flags = static_cast<uint8_t>(received.flags & ~IM_FLAG_TX_ECHO);
            g_receiveQueue.push_back(received);
        }
        g_queueChanged.notify_one();
        SetError("");
        return IM_OK;
    }
    catch (...)
    {
        SetError("Unexpected exception in IM_InjectRx.");
        return IM_ERR_IO;
    }
}
