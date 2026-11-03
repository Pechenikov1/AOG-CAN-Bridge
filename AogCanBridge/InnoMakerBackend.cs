using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;

namespace AogCanBridge
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct ImCanFrame
    {
        public uint Id;
        public byte Dlc;
        public byte Flags;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public byte[] Data;

        static ImCanFrame()
        {
            if (Marshal.SizeOf<ImCanFrame>() != 14)
                throw new InvalidOperationException("ImCanFrame ABI mismatch");
        }
    }

    internal static class Native
    {
        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int IM_Open(
            [MarshalAs(UnmanagedType.LPStr)] string devicePath, uint baudrate);

        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int IM_Send(ref ImCanFrame frame);

        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int IM_Receive(ref ImCanFrame frame, int timeoutMs);

        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern void IM_Close();

        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern IntPtr IM_LastError();

        [DllImport("InnoMakerNativeBridge.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int IM_InjectRx(ref ImCanFrame frame);
    }

    public sealed class InnoMakerBackend : ICanBackend, IDisposable
    {
        internal const uint ErrorNotImplemented = 0xE0000001;
        internal const uint ErrorNativeMissing = 0xE0000002;

        private const int ImErrorTimeout = -4;
        private readonly ConcurrentQueue<ImCanFrame> _rxQueue = new ConcurrentQueue<ImCanFrame>();
        private readonly string _devicePath;
        private readonly uint _baudrate;
        private Thread _rxThread;
        private volatile bool _running;
        private bool _opened;
        private string _lastError = string.Empty;

        public InnoMakerBackend()
            : this("mock", 250000)
        {
        }

        public InnoMakerBackend(string devicePath, uint baudrate)
        {
            _devicePath = devicePath ?? throw new ArgumentNullException(nameof(devicePath));
            _baudrate = baudrate;
        }

        public event Action<ImCanFrame>? FrameReceived;
        public event Action<string>? ErrorOccurred;

        public BackendMode Mode => BackendMode.InnoMakerUsb2Can;

        public bool Open()
        {
            if (_opened)
                return true;

            int rc;
            try
            {
                rc = Native.IM_Open(_devicePath, _baudrate);
            }
            catch (Exception ex)
            {
                _lastError = ex.Message;
                ErrorOccurred?.Invoke("IM_Open failed: " + ex.Message);
                return false;
            }

            if (rc != 0)
            {
                RaiseError("IM_Open", rc);
                return false;
            }

            _opened = true;
            _running = true;
            _rxThread = new Thread(RxLoop)
            {
                IsBackground = true,
                Name = "InnoMaker-RX"
            };
            _rxThread.Start();
            return true;
        }

        public bool Send(uint id, byte[] data, bool extended = false, bool rtr = false)
        {
            if (data == null || data.Length > 8)
            {
                ErrorOccurred?.Invoke("IM_Send failed: invalid data length");
                return false;
            }

            ImCanFrame frame = new ImCanFrame
            {
                Id = id,
                Dlc = (byte)data.Length,
                Flags = (byte)((extended ? 0x01 : 0) | (rtr ? 0x02 : 0)),
                Data = new byte[8]
            };
            Buffer.BlockCopy(data, 0, frame.Data, 0, data.Length);

            try
            {
                int rc = Native.IM_Send(ref frame);
                if (rc == 0)
                    return true;
                RaiseError("IM_Send", rc);
                return false;
            }
            catch (Exception ex)
            {
                _lastError = ex.Message;
                ErrorOccurred?.Invoke("IM_Send failed: " + ex.Message);
                return false;
            }
        }

        private void RxLoop()
        {
            while (_running)
            {
                ImCanFrame frame = new ImCanFrame { Data = new byte[8] };

                int rc;
                try
                {
                    rc = Native.IM_Receive(ref frame, 100);
                }
                catch (Exception ex)
                {
                    _lastError = ex.Message;
                    ErrorOccurred?.Invoke("IM_Receive failed: " + ex.Message);
                    continue;
                }

                if (rc == ImErrorTimeout)
                    continue;

                if (rc != 0)
                {
                    RaiseError("IM_Receive", rc);
                    continue;
                }

                if ((frame.Flags & 0x04) != 0)
                    continue;

                _rxQueue.Enqueue(frame);
                FrameReceived?.Invoke(frame);
            }
        }

        public void Close()
        {
            _running = false;
            if (_rxThread != null && _rxThread != Thread.CurrentThread)
                _rxThread.Join(500);
            _rxThread = null;

            if (_opened)
            {
                _opened = false;
                try
                {
                    Native.IM_Close();
                }
                catch (Exception ex)
                {
                    _lastError = ex.Message;
                    ErrorOccurred?.Invoke("IM_Close failed: " + ex.Message);
                }
            }
        }

        public void Dispose()
        {
            Close();
        }

        private void RaiseError(string op, int rc)
        {
            IntPtr p = IntPtr.Zero;
            try
            {
                p = Native.IM_LastError();
            }
            catch (Exception ex)
            {
                _lastError = ex.Message;
            }

            string msg = p == IntPtr.Zero ? string.Empty : (Marshal.PtrToStringAnsi(p) ?? string.Empty);
            if (!string.IsNullOrEmpty(msg))
                _lastError = msg;

            ErrorOccurred?.Invoke($"{op} failed rc={rc}: {msg}");
        }

        public uint Initialize(ushort channel, ushort baudrate)
        {
            if (baudrate != PcanBasic.Baud250K)
                return ErrorNotImplemented;

            return Open() ? PcanBasic.ErrorOk : ErrorNativeMissing;
        }

        public uint Uninitialize(ushort channel)
        {
            Close();
            return PcanBasic.ErrorOk;
        }

        public uint Read(ushort channel, out PcanBasic.CanMessage message,
            out PcanBasic.CanTimestamp timestamp)
        {
            timestamp = default(PcanBasic.CanTimestamp);

            if (!_rxQueue.TryDequeue(out ImCanFrame frame))
            {
                message = default(PcanBasic.CanMessage);
                return PcanBasic.ErrorReceiveQueueEmpty;
            }

            message = new PcanBasic.CanMessage
            {
                Id = frame.Id,
                MessageType = (byte)((frame.Flags & 0x01) != 0 ? PcanBasic.MessageExtended : 0),
                Length = frame.Dlc,
                Data = frame.Data ?? new byte[8]
            };
            return PcanBasic.ErrorOk;
        }

        public uint Write(ushort channel, ref PcanBasic.CanMessage message)
        {
            if (message.Data == null || message.Length > 8 || message.Data.Length < message.Length)
                return unchecked((uint)-2);

            byte[] data = new byte[message.Length];
            Buffer.BlockCopy(message.Data, 0, data, 0, message.Length);

            bool ok = Send(message.Id, data, (message.MessageType & PcanBasic.MessageExtended) != 0);
            return ok ? PcanBasic.ErrorOk : ErrorNativeMissing;
        }

        public string GetErrorText(uint error)
        {
            if (error == ErrorNotImplemented)
                return "InnoMaker USB2CAN-C backend is not implemented for this baudrate.";
            if (error == ErrorNativeMissing)
                return string.IsNullOrEmpty(_lastError)
                    ? "InnoMaker native wrapper is missing or failed." : _lastError;
            return "InnoMaker error 0x" + error.ToString("X");
        }
    }
}
