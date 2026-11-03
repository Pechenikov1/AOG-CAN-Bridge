using System.Runtime.InteropServices;
using AogCanBridge;
using Xunit;

namespace AogCanBridge.Tests;

public sealed class ImCanFrameAbiTests
{
    [Fact]
    public void ImCanFrame_Size_Is_14_Bytes()
    {
        Assert.Equal(14, Marshal.SizeOf<ImCanFrame>());
    }

    [Fact]
    public void ImCanFrame_Pack_Is_1()
    {
        Assert.Equal(1, (int)typeof(ImCanFrame).StructLayoutAttribute!.Pack);
    }

    [Fact]
    public void InnoMakerBackend_Mode_Is_InnoMakerUsb2Can()
    {
        using var backend = new InnoMakerBackend("mock", 250000);
        Assert.Equal(BackendMode.InnoMakerUsb2Can, backend.Mode);
    }
}
