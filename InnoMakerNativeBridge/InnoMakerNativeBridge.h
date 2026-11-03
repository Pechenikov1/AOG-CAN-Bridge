#pragma once
#include <cstdint>

#ifdef _WIN32
#define IM_CALL __cdecl
#else
#define IM_CALL
#endif

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

struct IM_HANDLE;

extern "C" {
int IM_CALL IM_Open(std::uint32_t device_index, std::uint32_t bitrate, IM_HANDLE** output);
int IM_CALL IM_Send(IM_HANDLE* handle, const IM_CAN_FRAME* frame);
int IM_CALL IM_Receive(IM_HANDLE* handle, IM_CAN_FRAME* output, std::uint32_t timeout_ms);
int IM_CALL IM_Close(IM_HANDLE* handle);
}
