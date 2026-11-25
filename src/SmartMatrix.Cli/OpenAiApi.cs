using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

namespace SmartMatrix.Cli;

internal sealed class OpenAiApi(HttpClient http, Uri baseUrl, string apiKey)
{
    private static readonly string FrameInstruction = $"""
        Draw the user's request as a single {RgbFrame.Width}x{RgbFrame.Height} pixel-art image.
        Use only these case-sensitive palette symbols: {FramePalette.Legend}.

        Plan the picture before writing the rows:
        1. Identify the main subject and any requested colors, pose, or composition. For a named
           character or object, pick two to four distinctive features and their signature colors.
        2. Make those features visible as separate blocks of pixels. Unless asked otherwise,
           draw one large subject against a simple background, with filled shapes and strong contrast.
           If details do not fit, simplify the pose or crop to a close-up that keeps the key features.
        3. Check that the finished grid shows the requested subject and that its key features
           are actually present. If not, revise the grid before answering. Never substitute an
           unrelated icon or placeholder. Use a fully black frame only when requested.

        Return only a JSON object with exactly one property, "rows". Its value is an array
        of exactly {RgbFrame.Height} strings, each exactly {RgbFrame.Width} characters long.
        Each character is one pixel. Write pixels left to right, and rows top to bottom.
        Do not include spaces inside rows, your plan, prose, comments, or Markdown fences.
        """;

    public static bool TryGetBaseUrl(string input, out Uri? baseUrl)
    {
        baseUrl = null;
        if (!Uri.TryCreate(input, UriKind.Absolute, out var parsed) ||
            (parsed.Scheme != Uri.UriSchemeHttps && parsed.Scheme != Uri.UriSchemeHttp) ||
            parsed.Host.Length == 0 || parsed.UserInfo.Length != 0 ||
            parsed.Query.Length != 0 || parsed.Fragment.Length != 0)
        {
            return false;
        }

        baseUrl = new Uri(parsed.AbsoluteUri.TrimEnd('/') + "/", UriKind.Absolute);
        return true;
    }

    public async Task<string[]> ListModelsAsync(CancellationToken cancellationToken)
    {
        using var request = CreateRequest(HttpMethod.Get, "models");
        using var document = await SendJsonAsync(request, cancellationToken);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("data", out var data) || data.ValueKind != JsonValueKind.Array)
        {
            throw new FormatException("Ответ /models не содержит массива data.");
        }

        return data.EnumerateArray()
            .Where(item => item.ValueKind == JsonValueKind.Object &&
                item.TryGetProperty("id", out var id) && id.ValueKind == JsonValueKind.String)
            .Select(item => item.GetProperty("id").GetString()!)
            .Where(id => !string.IsNullOrWhiteSpace(id))
            .Distinct(StringComparer.Ordinal)
            .OrderBy(id => id, StringComparer.Ordinal)
            .ToArray();
    }

    public async Task<string> GenerateAsync(string model, string prompt, CancellationToken cancellationToken)
    {
        using var request = CreateRequest(HttpMethod.Post, "chat/completions");
        request.Content = JsonContent.Create(new
        {
            model,
            messages = new[]
            {
                new { role = "system", content = FrameInstruction },
                new { role = "user", content = prompt }
            },
            stream = false
        });

        using var document = await SendJsonAsync(request, cancellationToken);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("choices", out var choices) ||
            choices.ValueKind != JsonValueKind.Array || choices.GetArrayLength() == 0 ||
            choices[0].ValueKind != JsonValueKind.Object ||
            !choices[0].TryGetProperty("message", out var message) || message.ValueKind != JsonValueKind.Object ||
            !message.TryGetProperty("content", out var content) || content.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(content.GetString()))
        {
            throw new FormatException("Модель не вернула текст в choices[0].message.content.");
        }

        return content.GetString()!;
    }

    private HttpRequestMessage CreateRequest(HttpMethod method, string path) =>
        new(method, new Uri(baseUrl, path))
        {
            Headers = { Authorization = new AuthenticationHeaderValue("Bearer", apiKey) }
        };

    private async Task<JsonDocument> SendJsonAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        using var response = await http.SendAsync(request, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException(
                $"API: {request.Method} {request.RequestUri!.AbsolutePath} вернул HTTP {(int)response.StatusCode}.",
                inner: null, response.StatusCode);
        }

        await using var body = await response.Content.ReadAsStreamAsync(cancellationToken);
        try
        {
            return await JsonDocument.ParseAsync(body, cancellationToken: cancellationToken);
        }
        catch (JsonException)
        {
            throw new FormatException("API вернул ответ не в формате JSON.");
        }
    }
}
