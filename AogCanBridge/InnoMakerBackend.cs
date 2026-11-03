using System;

namespace AogCanBridge
{
    internal sealed class InnoMakerBackend : ICanBackend
    {
        internal const uint ErrorNotImplemented = 0xE0000001;
        internal const uint ErrorNativeMissing = 0xE0000002;

        public BackendMode Mode => BackendMode.InnoMakerUsb2Can;

        public uint Initialize(ushort channel, ushort baudrate)
        {
            if (baudrate != PcanBasic.Baud250K)
                return ErrorNotImplemented;
            return ErrorNativeMissing;
        }

        public uint Uninitialize(ushort channel) => PcanBasic.ErrorOk;

        public uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp)
        {
            message = default(PcanBasic.CanMessage);
            timestamp = default(PcanBasic.CanTimestamp);
            return ErrorNativeMissing;
        }

        public uint Write(ushort channel, ref PcanBasic.CanMessage message) =>
            ErrorNativeMissing;

        public string GetErrorText(uint error)
        {
            if (error == ErrorNotImplemented)
                return "InnoMaker USB2CAN-C backend is not implemented yet.";
            if (error == ErrorNativeMissing)
                return "InnoMaker native wrapper is missing. Build the native wrapper before using this adapter.";
            return "InnoMaker error 0x" + error.ToString("X");
        }
    }
}
