namespace SmartMatrix.Cli;

internal static class FramePalette
{
    private static readonly (char Symbol, string Name, RgbPixel Pixel)[] Colors =
    [
        ('.', "black (unlit)", new(0, 0, 0)),
        ('W', "white", new(255, 255, 255)),
        ('R', "red", new(255, 0, 0)),
        ('O', "orange", new(255, 128, 0)),
        ('Y', "yellow", new(255, 255, 0)),
        ('G', "green", new(0, 255, 0)),
        ('C', "cyan", new(0, 255, 255)),
        ('B', "blue", new(0, 0, 255)),
        ('P', "purple", new(160, 0, 255)),
        ('M', "magenta", new(255, 0, 255)),
        ('S', "light skin tone", new(255, 204, 153)),
        ('N', "brown", new(160, 82, 45)),
        ('A', "gray", new(128, 128, 128)),
        ('D', "dark gray", new(64, 64, 64))
    ];

    public static string Legend { get; } =
        string.Join(", ", Colors.Select(color => $"{color.Symbol} = {color.Name}"));

    public static bool TryGetPixel(char symbol, out RgbPixel pixel)
    {
        foreach (var color in Colors)
        {
            if (color.Symbol == symbol)
            {
                pixel = color.Pixel;
                return true;
            }
        }

        pixel = default;
        return false;
    }

    public static char GetSymbol(RgbPixel pixel)
    {
        foreach (var color in Colors)
        {
            if (color.Pixel == pixel)
                return color.Symbol;
        }

        throw new ArgumentOutOfRangeException(nameof(pixel), "Цвет не входит в палитру.");
    }
}
