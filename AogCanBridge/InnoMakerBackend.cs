namespace AogCanBridge
{
    /// <summary>
    /// Selection placeholder for the InnoMaker USB2CAN-C backend.
    /// The official SDK is a C++ class library; its native C ABI wrapper and
    /// hardware operations are a separate implementation milestone. Until that
    /// wrapper exists, fail explicitly instead of pretending frames were sent.
    /// </summary>
    internal sealed class InnoMakerBackend : ICanBackend
    {
        internal const uint ErrorBackendNotImplemented = 0xE0000001;

        public BackendMode Mode => BackendMode.InnoMakerUsb2Can;

        public uint Initialize(ushort channel, ushort baudrate) => ErrorBackendNotImplemented;
        public uint Uninitialize(ushort channel) => PcanBasic.ErrorOk;

        public uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp)
        {
            message = default(PcanBasic.CanMessage);
            timestamp = default(PcanBasic.CanTimestamp);
            return ErrorBackendNotImplemented;
        }

        public uint Write(ushort channel, ref PcanBasic.CanMessage message) =>
            ErrorBackendNotImplemented;

        public string GetErrorText(uint error) =>
            error == ErrorBackendNotImplemented
                ? "InnoMaker USB2CAN-C backend is not implemented yet."
                : "InnoMaker error 0x" + error.ToString("X");
    }
}
