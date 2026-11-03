namespace AogCanBridge
{
    /// <summary>
    /// Physical CAN adapter contract used by the bridge broker. Frames use the
    /// existing Classic CAN representation so the client proxy and UDP protocol
    /// remain independent from the selected hardware.
    /// </summary>
    internal interface ICanBackend
    {
        BackendMode Mode { get; }
        uint Initialize(ushort channel, ushort baudrate);
        uint Uninitialize(ushort channel);
        uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp);
        uint Write(ushort channel, ref PcanBasic.CanMessage message);
        string GetErrorText(uint error);
    }
}
