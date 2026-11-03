using System;
using System.Runtime.InteropServices;

namespace AogCanBridge
{
    /// <summary>Adapter over the native C ABI wrapper around InnoMaker's SDK.</summary>
    internal sealed class InnoMakerBackend : ICanBackend
    {
        internal const uint ErrorNotInitialized = 0xE0000001;
        internal const uint ErrorNative = 0xE0000002;
        private const int NativeOk = 0;
        private const int NativeTimeout = 1;
        private const ushort DefaultChannel = 0;
        private IntPtr handle;

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct NativeCanFrame
        {
            internal uint Identifier;
            internal uint Flags;
            internal byte Dlc;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
            internal byte[] Data;
            internal uint TimestampUs;
        }

        private static class Native
        {
            [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl)]
            internal static extern int IM_Open(uint deviceIndex, uint bitrate, out IntPtr handle);
            [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl)]
            internal static extern int IM_Send(IntPtr handle, ref NativeCanFrame frame);
            [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl)]
            internal static extern int IM_Receive(IntPtr handle, out NativeCanFrame frame,
                uint timeoutMs);
            [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl)]
            internal static extern int IM_Close(IntPtr handle);
        }

        public BackendMode Mode => BackendMode.InnoMakerUsb2Can;

        public uint Initialize(ushort channel, ushort baudrate)
        {
            if (handle != IntPtr.Zero) return ErrorNotInitialized;
            if (baudrate != PcanBasic.Baud250K) return ErrorNative;
            try
            {
                int result = Native.IM_Open(DefaultChannel, 250000, out handle);
                return result == NativeOk && handle != IntPtr.Zero ? PcanBasic.ErrorOk : ErrorNative;
            }
            catch (DllNotFoundException) { return ErrorNative; }
            catch (EntryPointNotFoundException) { return ErrorNative; }
        }

        public uint Uninitialize(ushort channel)
        {
            if (handle == IntPtr.Zero) return PcanBasic.ErrorOk;
            try
            {
                int result = Native.IM_Close(handle);
                handle = IntPtr.Zero;
                return result == NativeOk ? PcanBasic.ErrorOk : ErrorNative;
            }
            catch (DllNotFoundException)
            {
                handle = IntPtr.Zero;
                return ErrorNative;
            }
        }

        public uint Write(ushort channel, ref PcanBasic.CanMessage message)
        {
            if (handle == IntPtr.Zero || message.Length > 8 || message.Data == null ||
                message.Data.Length < 8)
                return ErrorNotInitialized;

            NativeCanFrame frame = new NativeCanFrame
            {
                Identifier = message.Id,
                Flags = (message.MessageType & PcanBasic.MessageExtended) != 0 ? 1u : 0u,
                Dlc = message.Length,
                Data = new byte[8],
                TimestampUs = 0
            };
            Buffer.BlockCopy(message.Data, 0, frame.Data, 0, 8);
            try { return Native.IM_Send(handle, ref frame) == NativeOk ? PcanBasic.ErrorOk : ErrorNative; }
            catch (DllNotFoundException) { return ErrorNative; }
        }

        public uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp)
        {
            message = default(PcanBasic.CanMessage);
            timestamp = default(PcanBasic.CanTimestamp);
            if (handle == IntPtr.Zero) return ErrorNotInitialized;

            try
            {
                int result = Native.IM_Receive(handle, out NativeCanFrame frame, 0);
                if (result == NativeTimeout) return PcanBasic.ErrorReceiveQueueEmpty;
                if (result != NativeOk || frame.Dlc > 8 || frame.Data == null)
                    return ErrorNative;

                message = new PcanBasic.CanMessage
                {
                    Id = frame.Identifier,
                    MessageType = (byte)((frame.Flags & 1) != 0 ? PcanBasic.MessageExtended : 0),
                    Length = frame.Dlc,
                    Data = new byte[8]
                };
                Buffer.BlockCopy(frame.Data, 0, message.Data, 0, 8);
                timestamp.Millis = frame.TimestampUs / 1000;
                timestamp.Micros = (ushort)(frame.TimestampUs % 1000);
                return PcanBasic.ErrorOk;
            }
            catch (DllNotFoundException) { return ErrorNative; }
        }

        public string GetErrorText(uint error) => error == ErrorNotInitialized
            ? "InnoMaker backend is not initialized."
            : error == ErrorNative
                ? "InnoMaker native wrapper/SDK is missing or reported an error."
                : "InnoMaker error 0x" + error.ToString("X");
    }
}
