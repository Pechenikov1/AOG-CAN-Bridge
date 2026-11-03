#include "InnoMakerNativeBridge.h"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace
{
    std::deque<IM_CAN_FRAME> g_receiveQueue;
    std::mutex g_queueMutex;
    std::condition_variable g_queueChanged;
    bool g_mockMode = false;
    bool g_open = false;
    thread_local std::string g_lastError;

    void SetError(const char* message)
    {
        g_lastError = message ? message : "";
    }

    bool IsOpen()
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        return g_open;
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

        if (g_mockMode)
        {
            g_open = true;
            SetError("");
            return IM_OK;
        }

        // TODO: извикай InnoMakerUsb2CanLib::open/send/receive тук
        SetError("Real InnoMaker SDK integration is not implemented yet.");
        return IM_ERR_OPEN;
    }
    catch (...)
    {
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
            if (!g_mockMode)
            {
                // TODO: извикай InnoMakerUsb2CanLib::open/send/receive тук
                SetError("Real InnoMaker SDK integration is not implemented yet.");
                return IM_ERR_IO;
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
        if (!g_mockMode)
        {
            // TODO: извикай InnoMakerUsb2CanLib::open/send/receive тук
            SetError("Real InnoMaker SDK integration is not implemented yet.");
            return IM_ERR_IO;
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
            g_open = false;
            g_mockMode = false;
            g_receiveQueue.clear();
        }
        g_queueChanged.notify_all();
        SetError("");
    }
    catch (...)
    {
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
            if (!g_open || !g_mockMode)
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
