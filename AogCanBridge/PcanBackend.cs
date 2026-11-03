using System.Text;

namespace AogCanBridge
{
    /// <summary>Adapter over the existing PEAK PCAN-Basic API.</summary>
    internal sealed class PcanBackend : ICanBackend
    {
        public BackendMode Mode => BackendMode.Pcan;

        public uint Initialize(ushort channel, ushort baudrate) =>
            PcanBasic.Initialize(channel, baudrate);

        public uint Uninitialize(ushort channel) => PcanBasic.Uninitialize(channel);

        public uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp) => PcanBasic.Read(channel, out message, out timestamp);

        public uint Write(ushort channel, ref PcanBasic.CanMessage message) =>
            PcanBasic.Write(channel, ref message);

        public string GetErrorText(uint error)
        {
            StringBuilder text = new StringBuilder(256);
            return PcanBasic.GetErrorText(error, 0, text) == PcanBasic.ErrorOk
                ? text.ToString() : "PCAN error 0x" + error.ToString("X");
        }
    }
}
