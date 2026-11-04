#include "InnoMakerNativeBridge.h"
#include "InnoMakerUsb2CanLib.h"

#include <condition_variable>
#include <chrono>
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

    VCI_INIT_CONFIG g_initConfig{};
    VCI_CAN_OBJ g_sendObj{};
    VCI_CAN_OBJ g_recvObj{};
    DWORD g_deviceType = VCI_USBCAN2;
    DWORD g_deviceInd = 0;
    DWORD g_canInd = 0;
    bool g_realMode = false;

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

        if (g_mockMode)
        {
            g_open = true;
            SetError("");
            return IM_OK;
        }

        if (baudrate != 250000)
        {
            SetError("Only 250 kbit/s is configured for the InnoMaker SDK backend.");
            return IM_ERR_PARAM;
        }

        // The vendor API reports success as nonzero for OpenDevice, InitCAN, and StartCAN.
        if (VCI_OpenDevice(g_deviceType, g_deviceInd, 0) == 0)
        {
            SetError("VCI_OpenDevice failed.");
            return IM_ERR_OPEN;
        }

        g_initConfig.AccCode = 0;
        g_initConfig.AccMask = 0xFFFFFFFF;
        g_initConfig.Filter = 0;
        g_initConfig.Timing0 = 0x01;
        g_initConfig.Timing1 = 0x1C;
        g_initConfig.Mode = 0;

        if (VCI_InitCAN(g_deviceType, g_deviceInd, g_canInd, &g_initConfig) == 0)
        {
            VCI_CloseDevice(g_deviceType, g_deviceInd);
            SetError("VCI_InitCAN failed.");
            return IM_ERR_OPEN;
        }

        if (VCI_StartCAN(g_deviceType, g_deviceInd, g_canInd) == 0)
        {
            VCI_CloseDevice(g_deviceType, g_deviceInd);
            SetError("VCI_StartCAN failed.");
            return IM_ERR_OPEN;
        }

        g_realMode = true;
        g_open = true;
        SetError("");
        return IM_OK;
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
            if (g_realMode)
            {
                std::memset(&g_sendObj, 0, sizeof(g_sendObj));
                g_sendObj.ID = frame->id;
                g_sendObj.SendType = 0;
                g_sendObj.RemoteFlag = (frame->flags & IM_FLAG_RTR) ? 1 : 0;
                g_sendObj.ExternFlag = (frame->flags & IM_FLAG_EXTENDED) ? 1 : 0;
                g_sendObj.DataLen = frame->dlc;
                std::memcpy(g_sendObj.Data, frame->data, sizeof(g_sendObj.Data));

                // VCI_Transmit returns the number of frames transmitted; zero indicates failure.
                if (VCI_Transmit(g_deviceType, g_deviceInd, g_canInd, &g_sendObj, 1) == 0)
                {
                    SetError("VCI_Transmit failed.");
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
            std::memset(&g_recvObj, 0, sizeof(g_recvObj));
            const DWORD received = VCI_Receive(g_deviceType, g_deviceInd, g_canInd,
                &g_recvObj, 1, static_cast<int>(timeoutMs));
            if (received == 0)
            {
                SetError("VCI_Receive timed out or failed to receive a frame.");
                return IM_ERR_TIMEOUT;
            }

            frame->id = g_recvObj.ID;
            frame->dlc = static_cast<uint8_t>(g_recvObj.DataLen > 8 ? 8 : g_recvObj.DataLen);
            frame->flags = 0;
            if (g_recvObj.ExternFlag)
                frame->flags = static_cast<uint8_t>(frame->flags | IM_FLAG_EXTENDED);
            if (g_recvObj.RemoteFlag)
                frame->flags = static_cast<uint8_t>(frame->flags | IM_FLAG_RTR);
            std::memcpy(frame->data, g_recvObj.Data, sizeof(frame->data));
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
                VCI_CloseDevice(g_deviceType, g_deviceInd);
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
