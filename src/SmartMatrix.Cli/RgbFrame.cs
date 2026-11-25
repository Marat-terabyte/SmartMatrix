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
            throw new FormatException("Ожидался JSON-объект с единственным полем rows.");

        var properties = root.EnumerateObject();
        if (!properties.MoveNext() || !properties.Current.NameEquals("rows"))
            throw new FormatException("Ожидался JSON-объект с единственным полем rows.");

        var source = properties.Current.Value;
        if (properties.MoveNext())
            throw new FormatException("В JSON допустимо только одно поле rows.");

        if (source.ValueKind != JsonValueKind.Array || source.GetArrayLength() != Height)
            throw new FormatException($"В rows должно быть ровно {Height} строк.");

        var pixels = new RgbPixel[PixelCount];
        var y = 0;
        foreach (var item in source.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || item.GetString() is not { Length: Width } row)
                throw new FormatException($"Строка {y + 1}: ожидается ровно {Width} символов палитры.");

            for (var x = 0; x < Width; x++)
            {
                if (!FramePalette.TryGetPixel(row[x], out pixels[y * Width + x]))
                    throw new FormatException($"Строка {y + 1}, столбец {x + 1}: символ не входит в палитру.");
            }

            y++;
        }

        return new RgbFrame(pixels);
    }
}
