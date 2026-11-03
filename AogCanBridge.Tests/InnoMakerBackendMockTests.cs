using AogCanBridge;
using Xunit;

namespace AogCanBridge.Tests;

[CollectionDefinition("InnoMaker native singleton", DisableParallelization = true)]
public sealed class InnoMakerNativeCollection
{
}

[Collection("InnoMaker native singleton")]
public sealed class InnoMakerBackendMockTests
{
    private static InnoMakerBackend CreateOpenedBackend()
    {
        var backend = new InnoMakerBackend("mock", 250000);
        Assert.Equal(PcanBasic.ErrorOk,
            backend.Initialize(PcanBasic.PcanUsbBus1, PcanBasic.Baud250K));
        return backend;
    }

    [Fact]
    public void Open_Mock_Returns_ErrorOk()
    {
        using var backend = new InnoMakerBackend("mock", 250000);
        Assert.Equal(PcanBasic.ErrorOk,
            backend.Initialize(PcanBasic.PcanUsbBus1, PcanBasic.Baud250K));
    }

    [Fact]
    public void Write_Echo_Is_Filtered_From_Read()
    {
        using var backend = CreateOpenedBackend();
        var message = new PcanBasic.CanMessage
        {
            Id = 0x123,
            Length = 2,
            Data = new byte[] { 0x12, 0x34, 0, 0, 0, 0, 0, 0 }
        };

        Assert.Equal(PcanBasic.ErrorOk,
            backend.Write(PcanBasic.PcanUsbBus1, ref message));

        // The RX worker consumes the native TX echo and filters it.
        Thread.Sleep(250);
        Assert.Equal(PcanBasic.ErrorReceiveQueueEmpty,
            backend.Read(PcanBasic.PcanUsbBus1, out _, out _));
    }

    [Fact]
    public void Read_Empty_Queue_Returns_QueueEmpty()
    {
        using var backend = CreateOpenedBackend();
        Assert.Equal(PcanBasic.ErrorReceiveQueueEmpty,
            backend.Read(PcanBasic.PcanUsbBus1, out _, out _));
    }

    [Fact]
    public void InjectRx_Appears_In_Read()
    {
        using var backend = CreateOpenedBackend();
        var injected = new ImCanFrame
        {
            Id = 0x456,
            Dlc = 3,
            Flags = 0x01,
            Data = new byte[] { 0xA1, 0xB2, 0xC3, 0, 0, 0, 0, 0 }
        };
        Assert.Equal(0, Native.IM_InjectRx(ref injected));

        PcanBasic.CanMessage received = default;
        uint result = PcanBasic.ErrorReceiveQueueEmpty;
        var deadline = DateTime.UtcNow.AddSeconds(2);
        while (DateTime.UtcNow < deadline && result == PcanBasic.ErrorReceiveQueueEmpty)
        {
            result = backend.Read(PcanBasic.PcanUsbBus1, out received, out _);
            if (result == PcanBasic.ErrorReceiveQueueEmpty)
                Thread.Sleep(10);
        }

        Assert.Equal(PcanBasic.ErrorOk, result);
        Assert.Equal(injected.Id, received.Id);
        Assert.Equal(injected.Dlc, received.Length);
        Assert.Equal(PcanBasic.MessageExtended, received.MessageType);
        Assert.Equal(new byte[] { 0xA1, 0xB2, 0xC3 }, received.Data.Take(3));
    }

    [Fact]
    public void Initialize_Rejects_Non250K_Baudrate()
    {
        using var backend = new InnoMakerBackend("mock", 250000);
        Assert.Equal(InnoMakerBackend.ErrorNotImplemented,
            backend.Initialize(PcanBasic.PcanUsbBus1, 0x001C));
    }
}
