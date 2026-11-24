using System.Text.Json;

namespace SmartMatrix.Cli;

internal readonly record struct RgbPixel(byte R, byte G, byte B);

internal sealed class RgbFrame
{
    public const int Width = 16;
    public const int Height = 16;
    public const int PixelCount = Width * Height;

    private readonly RgbPixel[] pixels;

    private RgbFrame(RgbPixel[] pixels) => this.pixels = pixels;

    public ReadOnlySpan<RgbPixel> Pixels => pixels;

    public static RgbFrame CreateTestPattern()
    {
        var pixels = new RgbPixel[PixelCount];
        pixels[0] = new RgbPixel(255, 0, 0);
        pixels[Width - 1] = new RgbPixel(0, 255, 0);
        pixels[(Height - 1) * Width] = new RgbPixel(0, 0, 255);
        pixels[^1] = new RgbPixel(255, 255, 255);
        return new RgbFrame(pixels);
    }

    public static RgbFrame ParseModelJson(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
            throw new FormatException("Ожидался JSON-объект с единственным полем pixels.");

        var properties = root.EnumerateObject();
        if (!properties.MoveNext() || !properties.Current.NameEquals("pixels"))
            throw new FormatException("Ожидался JSON-объект с единственным полем pixels.");

        var source = properties.Current.Value;
        if (properties.MoveNext())
            throw new FormatException("В JSON допустимо только одно поле pixels.");

        if (source.ValueKind != JsonValueKind.Array || source.GetArrayLength() != PixelCount)
            throw new FormatException($"В pixels должно быть ровно {PixelCount} пикселей.");

        var pixels = new RgbPixel[PixelCount];
        var index = 0;
        foreach (var pixel in source.EnumerateArray())
        {
            if (pixel.ValueKind != JsonValueKind.Array || pixel.GetArrayLength() != 3 ||
                !TryReadChannel(pixel[0], out var r) ||
                !TryReadChannel(pixel[1], out var g) ||
                !TryReadChannel(pixel[2], out var b))
            {
                throw new FormatException($"Пиксель {index + 1}: ожидается [R,G,B] с целыми числами от 0 до 255.");
            }

            pixels[index++] = new RgbPixel(r, g, b);
        }

        return new RgbFrame(pixels);
    }

    private static bool TryReadChannel(JsonElement channel, out byte value)
    {
        value = 0;
        return channel.ValueKind == JsonValueKind.Number && channel.TryGetByte(out value);
    }
}
