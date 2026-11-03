using System;
using System.Runtime.InteropServices;

namespace AogCanBridge
{
    /// <summary>
    /// The Classic CAN frame layout used by the official InnoMaker Windows SDK.
    /// The SDK's native structure is 24 bytes: echo/id, four control bytes,
    /// eight data bytes, then a 32-bit microsecond timestamp.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct InnoMakerHostFrame
    {
        internal uint EchoId;
        internal uint CanId;
        internal byte CanDlc;
        internal byte Channel;
        internal byte Flags;
        internal byte Reserved;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        internal byte[] Data;
        internal uint TimestampUs;
    }

    /// <summary>
    /// Converts the broker's existing frame representation to and from the
    /// InnoMaker USB2CAN Classic CAN host-frame layout. This codec does not open
    /// USB hardware; the native SDK adapter will own that responsibility.
    /// </summary>
    internal static class InnoMakerFrameCodec
    {
        internal const uint ReceiveFrameEchoId = 0xFFFFFFFF;
        internal const uint CanEffFlag = 0x80000000;
        internal const uint CanRtrFlag = 0x40000000;
        internal const uint CanErrFlag = 0x20000000;
        internal const uint CanIdMask = 0x1FFFFFFF;
        internal const int HostFrameSize = 24;

        /// <summary>
        /// Converts a broker packet into an InnoMaker transmit frame. The SDK
        /// transmit-context allocator must provide echoId before sending.
        /// Only Classic CAN data frames are represented by the current bridge
        /// protocol; remote/error frames are rejected rather than misencoded.
        /// </summary>
        internal static bool TryFromBridgePacket(BridgePacket packet, uint echoId,
            out InnoMakerHostFrame frame)
        {
            frame = default(InnoMakerHostFrame);
            if (packet == null || packet.Type != BridgeMessageType.FrameFromClient ||
                packet.DataLength > 8 || (packet.Flags & ~1) != 0)
                return false;

            bool extended = (packet.Flags & 1) != 0;
            uint maximumId = extended ? 0x1FFFFFFF : 0x7FF;
            if (packet.Identifier > maximumId) return false;

            frame = new InnoMakerHostFrame
            {
                EchoId = echoId,
                CanId = packet.Identifier | (extended ? CanEffFlag : 0),
                CanDlc = packet.DataLength,
                Channel = 0,
                Flags = 0,
                Reserved = 0,
                Data = new byte[8],
                TimestampUs = 0
            };
            Buffer.BlockCopy(packet.Data, 0, frame.Data, 0, packet.DataLength);
            return true;
        }

        /// <summary>
        /// Converts a received bus frame into the broker's broadcast packet.
        /// InnoMaker also reports transmit completions through the same receive
        /// endpoint; those have a non-FFFFFFFF echo ID and are deliberately
        /// ignored here instead of being mistaken for bus traffic.
        /// </summary>
        internal static bool TryToBridgePacket(InnoMakerHostFrame frame,
            out BridgePacket packet)
        {
            packet = null;
            if (frame.EchoId != ReceiveFrameEchoId || frame.CanDlc > 8 ||
                frame.Data == null || frame.Data.Length < 8)
                return false;

            // The current proxy protocol has no representation for RTR or CAN
            // error frames. Do not silently turn either into an ordinary frame.
            if ((frame.CanId & (CanRtrFlag | CanErrFlag)) != 0) return false;

            bool extended = (frame.CanId & CanEffFlag) != 0;
            uint identifier = frame.CanId & CanIdMask;
            if (!extended && identifier > 0x7FF) return false;

            packet = new BridgePacket
            {
                Type = BridgeMessageType.FrameToClient,
                ClientId = 0,
                Identifier = identifier,
                TimestampUs = frame.TimestampUs,
                DataLength = frame.CanDlc,
                Flags = (byte)(extended ? 1 : 0)
            };
            Buffer.BlockCopy(frame.Data, 0, packet.Data, 0, 8);
            return true;
        }

        /// <summary>
        /// Serializes the fixed-layout structure for the vendor SDK's USB
        /// transfer functions. Keeps the SDK ABI boundary in one place.
        /// </summary>
        internal static byte[] Serialize(InnoMakerHostFrame frame)
        {
            if (frame.Data == null || frame.Data.Length != 8)
                throw new ArgumentException("InnoMaker frame data must contain exactly 8 bytes.",
                    nameof(frame));

            int size = Marshal.SizeOf(typeof(InnoMakerHostFrame));
            if (size != HostFrameSize)
                throw new InvalidOperationException("Unexpected InnoMaker host-frame ABI size: " + size);

            IntPtr memory = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(frame, memory, false);
                byte[] bytes = new byte[size];
                Marshal.Copy(memory, bytes, 0, size);
                return bytes;
            }
            finally
            {
                Marshal.DestroyStructure(memory, typeof(InnoMakerHostFrame));
                Marshal.FreeHGlobal(memory);
            }
        }

        /// <summary>Deserializes one complete vendor host-frame record.</summary>
        internal static bool TryDeserialize(byte[] bytes, out InnoMakerHostFrame frame)
        {
            frame = default(InnoMakerHostFrame);
            if (bytes == null || bytes.Length != HostFrameSize) return false;

            IntPtr memory = Marshal.AllocHGlobal(HostFrameSize);
            try
            {
                Marshal.Copy(bytes, 0, memory, HostFrameSize);
                frame = (InnoMakerHostFrame)Marshal.PtrToStructure(memory,
                    typeof(InnoMakerHostFrame));
                return frame.Data != null && frame.Data.Length == 8;
            }
            finally
            {
                Marshal.DestroyStructure(memory, typeof(InnoMakerHostFrame));
                Marshal.FreeHGlobal(memory);
            }
        }
    }
}
