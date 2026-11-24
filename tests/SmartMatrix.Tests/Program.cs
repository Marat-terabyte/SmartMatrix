using System.Net;
using System.Text;
using System.Text.Json;
using SmartMatrix.Cli;

var tests = new (string Name, Func<Task> Run)[]
{
    ("frame keeps RGB values and row-major order", Sync(() =>
    {
        var frame = RgbFrame.ParseModelJson(FrameJson(first: "[255,0,1]", last: "[2,3,4]"));
        Equal(256, frame.Pixels.Length);
        Equal(new RgbPixel(255, 0, 1), frame.Pixels[0]);
        Equal(new RgbPixel(0, 0, 0), frame.Pixels[16]);
        Equal(new RgbPixel(2, 3, 4), frame.Pixels[255]);
    })),
    ("frame rejects an incorrect pixel count", Sync(() =>
    {
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(count: 255)));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(count: 257)));
    })),
    ("frame rejects invalid RGB triples", Sync(() =>
    {
        foreach (var pixel in new[] { "[1,2]", "[1,2,3,4]", "[256,0,0]", "[-1,0,0]",
                     "[1.5,0,0]", "[1e0,0,0]", "[\"1\",0,0]", "[true,0,0]", "null" })
        {
            Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(first: pixel)));
        }
    })),
    ("frame rejects missing, unknown and duplicate properties", Sync(() =>
    {
        Throws<FormatException>(() => RgbFrame.ParseModelJson("[]"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson("{}"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson()[..^1] + ",\"extra\":0}"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson()[..^1] + ",\"pixels\":[]}"));
    })),
    ("frame rejects malformed JSON and Markdown fences", Sync(() =>
    {
        Throws<JsonException>(() => RgbFrame.ParseModelJson(FrameJson()[..^1]));
        Throws<JsonException>(() => RgbFrame.ParseModelJson($"```json\n{FrameJson()}\n```"));
    })),
    ("base URL preserves the API path and only accepts plain HTTP(S) URLs", Sync(() =>
    {
        Equal(true, OpenAiApi.TryGetBaseUrl("https://example.test/gateway/v1", out var url));
        Equal("https://example.test/gateway/v1/", url!.AbsoluteUri);
        Equal(true, OpenAiApi.TryGetBaseUrl("http://localhost:11434/v1/", out url));
        Equal("http://localhost:11434/v1/", url!.AbsoluteUri);
        foreach (var invalid in new[] { "/v1", "file:///v1", "https://user:secret@example.test/v1",
                     "https://example.test/v1?key=x", "https://example.test/v1#fragment" })
            Equal(false, OpenAiApi.TryGetBaseUrl(invalid, out _));
    })),
    ("API lists unique model IDs using the configured path and key", async () =>
    {
        using var http = new HttpClient(new FakeHandler((request, _) =>
        {
            Equal(HttpMethod.Get, request.Method);
            Equal("/gateway/v1/models", request.RequestUri!.AbsolutePath);
            Equal("Bearer example-key", request.Headers.Authorization!.ToString());
            return Task.FromResult(JsonResponse("""{"data":[{"id":"z"},{"id":"a"},{"id":"z"},{"id":42}]}"""));
        }));
        Equal("a,z", string.Join(",", await CreateApi(http).ListModelsAsync(CancellationToken.None)));
    }),
    ("API submits the prompt and returns the model's JSON content", async () =>
    {
        var expected = FrameJson(first: "[0,10,20]");
        using var http = new HttpClient(new FakeHandler(async (request, token) =>
        {
            Equal(HttpMethod.Post, request.Method);
            Equal("/gateway/v1/chat/completions", request.RequestUri!.AbsolutePath);
            Equal("Bearer example-key", request.Headers.Authorization!.ToString());
            using var payload = JsonDocument.Parse(await request.Content!.ReadAsStringAsync(token));
            var root = payload.RootElement;
            Equal("example-model", root.GetProperty("model").GetString());
            Equal(false, root.GetProperty("stream").GetBoolean());
            var messages = root.GetProperty("messages");
            Equal("system", messages[0].GetProperty("role").GetString());
            Equal(true, messages[0].GetProperty("content").GetString()!.Contains("256"));
            Equal("user", messages[1].GetProperty("role").GetString());
            Equal("Нарисуй кота", messages[1].GetProperty("content").GetString());
            return JsonResponse(JsonSerializer.Serialize(new
            {
                choices = new[] { new { message = new { content = expected } } }
            }));
        }));
        var result = await CreateApi(http).GenerateAsync("example-model", "Нарисуй кота", CancellationToken.None);
        Equal(expected, result);
        Equal(new RgbPixel(0, 10, 20), ReadFirstPixel(result));
    }),
    ("API reports missing model content", async () =>
    {
        using var http = new HttpClient(new FakeHandler((_, _) =>
            Task.FromResult(JsonResponse("""{"choices":[{"message":{"content":null}}]}"""))));
        await ThrowsAsync<FormatException>(() =>
            CreateApi(http).GenerateAsync("example-model", "test", CancellationToken.None));
    }),
    ("API distinguishes unauthorized and malformed responses", async () =>
    {
        using var unauthorized = new HttpClient(new FakeHandler((_, _) =>
            Task.FromResult(new HttpResponseMessage(HttpStatusCode.Unauthorized))));
        var error = await ThrowsAsync<HttpRequestException>(() =>
            CreateApi(unauthorized).ListModelsAsync(CancellationToken.None));
        Equal(HttpStatusCode.Unauthorized, error.StatusCode);

        using var invalidJson = new HttpClient(new FakeHandler((_, _) =>
            Task.FromResult(JsonResponse("<html>not an API</html>"))));
        await ThrowsAsync<FormatException>(() => CreateApi(invalidJson).ListModelsAsync(CancellationToken.None));
    }),
    ("transport sends exactly 768 RGB bytes to POST /frame", async () =>
    {
        var address = IPAddress.Parse("192.168.1.42");
        var frame = RgbFrame.ParseModelJson(FrameJson(first: "[255,0,1]", last: "[2,3,4]"));
        using var http = new HttpClient(new FakeHandler(async (request, token) =>
        {
            Equal(HttpMethod.Post, request.Method);
            Equal($"http://{address}/frame", request.RequestUri!.AbsoluteUri);
            Equal("application/octet-stream", request.Content!.Headers.ContentType!.MediaType);
            Equal(768L, request.Content.Headers.ContentLength);
            Equal(null, request.Headers.Authorization);
            var body = await request.Content.ReadAsByteArrayAsync(token);
            Equal(768, body.Length);
            Equal((byte)255, body[0]);
            Equal((byte)0, body[1]);
            Equal((byte)1, body[2]);
            Equal(true, body.Skip(3).Take(762).All(value => value == 0));
            Equal((byte)2, body[765]);
            Equal((byte)3, body[766]);
            Equal((byte)4, body[767]);
            return new HttpResponseMessage(HttpStatusCode.NoContent);
        }));
        await new HttpFrameTransport(http).SendAsync(address, frame, CancellationToken.None);
    }),
    ("transport treats only HTTP 204 as confirmation", async () =>
    {
        foreach (var status in new[] { HttpStatusCode.OK, HttpStatusCode.BadRequest, HttpStatusCode.Redirect })
        {
            using var http = new HttpClient(new FakeHandler((_, _) =>
                Task.FromResult(new HttpResponseMessage(status))));
            var error = await ThrowsAsync<HttpRequestException>(() =>
                new HttpFrameTransport(http).SendAsync(IPAddress.Loopback, RgbFrame.CreateTestPattern(),
                    CancellationToken.None));
            Equal(status, error.StatusCode);
        }
    }),
    ("test frame colors only the four corners", Sync(() =>
    {
        var pixels = RgbFrame.CreateTestPattern().Pixels;
        Equal(new RgbPixel(255, 0, 0), pixels[0]);
        Equal(new RgbPixel(0, 255, 0), pixels[15]);
        Equal(new RgbPixel(0, 0, 255), pixels[240]);
        Equal(new RgbPixel(255, 255, 255), pixels[255]);
        foreach (var index in Enumerable.Range(0, pixels.Length).Except([0, 15, 240, 255]))
            Equal(new RgbPixel(0, 0, 0), pixels[index]);
    }))
};

var failed = 0;
foreach (var test in tests)
{
    try
    {
        await test.Run();
        Console.WriteLine($"PASS {test.Name}");
    }
    catch (Exception ex)
    {
        failed++;
        Console.Error.WriteLine($"FAIL {test.Name}: {ex.Message}");
    }
}

Console.WriteLine($"{tests.Length - failed}/{tests.Length} tests passed.");
return failed == 0 ? 0 : 1;

static Func<Task> Sync(Action run) => () =>
{
    run();
    return Task.CompletedTask;
};

static string FrameJson(int count = 256, string first = "[0,0,0]", string last = "[0,0,0]") =>
    "{\"pixels\":[" + string.Join(",", Enumerable.Range(0, count).Select(i => i == 0 ? first :
        i == count - 1 ? last : "[0,0,0]")) + "]}";

static RgbPixel ReadFirstPixel(string json) => RgbFrame.ParseModelJson(json).Pixels[0];

static OpenAiApi CreateApi(HttpClient http) =>
    new(http, new Uri("https://example.test/gateway/v1/"), "example-key");

static HttpResponseMessage JsonResponse(string content) =>
    new(HttpStatusCode.OK) { Content = new StringContent(content, Encoding.UTF8, "application/json") };

static void Equal<T>(T expected, T actual)
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
        throw new Exception($"Expected {expected}; got {actual}.");
}

static void Throws<T>(Action run) where T : Exception
{
    try { run(); }
    catch (T) { return; }
    throw new Exception($"Expected {typeof(T).Name}.");
}

static async Task<T> ThrowsAsync<T>(Func<Task> run) where T : Exception
{
    try { await run(); }
    catch (T ex) { return ex; }
    throw new Exception($"Expected {typeof(T).Name}.");
}

sealed class FakeHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> send)
    : HttpMessageHandler
{
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) =>
        send(request, cancellationToken);
}
