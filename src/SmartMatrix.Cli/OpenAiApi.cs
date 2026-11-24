using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

namespace SmartMatrix.Cli;

internal sealed class OpenAiApi(HttpClient http, Uri baseUrl, string apiKey)
{
    private const string FrameInstruction = """
        Draw the user's request as a single 16x16 image. Return only a valid JSON object
        with exactly one property, "pixels". Its value must contain exactly 256 RGB triples,
        each an array [R,G,B] of three integers from 0 through 255. Start at the top left;
        enumerate pixels left to right in every row, then top to bottom. Use [0,0,0] for
        unlit pixels. Do not include coordinates, comments, prose, or Markdown fences.
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
