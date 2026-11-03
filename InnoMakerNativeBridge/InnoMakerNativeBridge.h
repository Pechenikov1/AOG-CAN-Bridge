/*
 * ABI CONTRACT — layout is critical. The C# side uses
 * [StructLayout(LayoutKind.Sequential, Pack=1)] and Marshal.SizeOf == 14.
 */
#ifndef INNOMAKER_NATIVE_BRIDGE_H
#define INNOMAKER_NATIVE_BRIDGE_H

#include <stdint.h>

#ifdef _WIN32
#define IM_API __declspec(dllexport)
#else
#define IM_API
#endif

#ifdef __cplusplus
#define IM_EXTERN_C extern "C"
#else
#define IM_EXTERN_C extern
#endif

#pragma pack(push, 1)
typedef struct IM_CAN_FRAME
{
    uint32_t id;
    uint8_t dlc;
    uint8_t flags;
    uint8_t data[8];
} IM_CAN_FRAME;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(IM_CAN_FRAME) == 14, "IM_CAN_FRAME ABI must be exactly 14 bytes");
#else
_Static_assert(sizeof(IM_CAN_FRAME) == 14, "IM_CAN_FRAME ABI must be exactly 14 bytes");
#endif

enum
{
    IM_FLAG_EXTENDED = 0x01,
    IM_FLAG_RTR = 0x02,
    IM_FLAG_TX_ECHO = 0x04
};

enum
{
    IM_OK = 0,
    IM_ERR_OPEN = -1,
    IM_ERR_PARAM = -2,
    IM_ERR_IO = -3,
    IM_ERR_TIMEOUT = -4
};

#ifdef __cplusplus
extern "C" {
#endif

IM_API int IM_Open(const char* devicePath, uint32_t baudrate);
IM_API int IM_Send(const IM_CAN_FRAME* frame);
IM_API int IM_Receive(IM_CAN_FRAME* frame, int timeoutMs);
IM_API void IM_Close(void);
IM_API const char* IM_LastError(void);

#ifdef __cplusplus
}
#endif

#endif /* INNOMAKER_NATIVE_BRIDGE_H */
