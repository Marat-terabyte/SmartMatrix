using System.Net;
using System.Text;
using System.Text.Json;
using SmartMatrix.Cli;

var tests = new (string Name, Func<Task> Run)[]
{
    ("frame expands palette colors in row-major order", Sync(() =>
    {
        var frame = RgbFrame.ParseModelJson(FrameJson(first: 'S', last: 'N'));
        Equal(256, frame.Pixels.Length);
        Equal(new RgbPixel(255, 204, 153), frame.Pixels[0]);
        Equal(new RgbPixel(0, 0, 0), frame.Pixels[16]);
        Equal(new RgbPixel(160, 82, 45), frame.Pixels[255]);
    })),
    ("frame rejects an incorrect row count or length", Sync(() =>
    {
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(count: 15)));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(count: 17)));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson().Replace(new string('.', 16),
            new string('.', 15))));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson().Replace(new string('.', 16),
            new string('.', 17))));
    })),
    ("frame rejects unknown palette symbols and rows that are not strings", Sync(() =>
    {
        foreach (var symbol in new[] { '?', 'r', ' ', '#', 'Я' })
            Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson(first: symbol)));

        foreach (var row in new[] { "null", "42", "[]", "true" })
        {
            Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson().Replace(
                $"\"{new string('.', 16)}\"", row)));
        }
    })),
    ("frame rejects missing, unknown and duplicate properties", Sync(() =>
    {
        Throws<FormatException>(() => RgbFrame.ParseModelJson("[]"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson("{}"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson()[..^1] + ",\"extra\":0}"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson(FrameJson()[..^1] + ",\"rows\":[]}"));
        Throws<FormatException>(() => RgbFrame.ParseModelJson("""{"pixels":[]}"""));
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
        var expected = FrameJson(first: 'B');
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
            var instruction = messages[0].GetProperty("content").GetString()!;
            Equal(true, instruction.Contains("\"rows\""));
            Equal(true, instruction.Contains("16 strings, each exactly 16 characters long"));
            Equal(true, instruction.Contains(FramePalette.Legend));
            Equal("user", messages[1].GetProperty("role").GetString());
            Equal("Нарисуй кота", messages[1].GetProperty("content").GetString());
            return JsonResponse(JsonSerializer.Serialize(new
            {
                choices = new[] { new { message = new { content = expected } } }
            }));
        }));
        var result = await CreateApi(http).GenerateAsync("example-model", "Нарисуй кота", CancellationToken.None);
        Equal(expected, result);
        Equal(new RgbPixel(0, 0, 255), RgbFrame.ParseModelJson(result).Pixels[0]);
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
        var frame = RgbFrame.ParseModelJson(FrameJson(first: 'S', last: 'N'));
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
            Equal((byte)204, body[1]);
            Equal((byte)153, body[2]);
            Equal(true, body.Skip(3).Take(762).All(value => value == 0));
            Equal((byte)160, body[765]);
            Equal((byte)82, body[766]);
            Equal((byte)45, body[767]);
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
    })),
    ("drawing session sends successive prompts to the same model and board until a blank line", async () =>
    {
        var address = IPAddress.Parse("192.168.1.42");
        var prompts = new List<string>();
        var sentGreenValues = new List<byte>();
        using var apiHttp = new HttpClient(new FakeHandler(async (request, token) =>
        {
            Equal(HttpMethod.Post, request.Method);
            Equal("Bearer example-key", request.Headers.Authorization!.ToString());
            using var payload = JsonDocument.Parse(await request.Content!.ReadAsStringAsync(token));
            Equal("example-model", payload.RootElement.GetProperty("model").GetString());
            var messages = payload.RootElement.GetProperty("messages");
            Equal(2, messages.GetArrayLength());
            prompts.Add(messages[1].GetProperty("content").GetString()!);
            return ModelResponse(FrameJson(first: prompts.Count == 1 ? 'R' : 'O'));
        }));
        using var matrixHttp = new HttpClient(new FakeHandler(async (request, token) =>
        {
            Equal($"http://{address}/frame", request.RequestUri!.AbsoluteUri);
            sentGreenValues.Add((await request.Content!.ReadAsByteArrayAsync(token))[1]);
            return new HttpResponseMessage(HttpStatusCode.NoContent);
        }));
        using var input = new StringReader("  Кот \nДом\n\nНе отправлять\n");
        using var output = new StringWriter();
        using var errors = new StringWriter();
        var session = new DrawingSession(new HttpFrameTransport(matrixHttp), address, output, errors);

        Equal(0, await session.RunAsync(CreateApi(apiHttp), "example-model", input, CancellationToken.None));
        Equal("Кот,Дом", string.Join(",", prompts));
        Equal("0,128", string.Join(",", sentGreenValues));
        Equal(string.Empty, errors.ToString());
    }),
    ("drawing session skips invalid frames and continues after a send failure until EOF", async () =>
    {
        var generated = 0;
        var sentGreenValues = new List<byte>();
        using var apiHttp = new HttpClient(new FakeHandler((_, _) =>
        {
            generated++;
            var frame = generated == 1 ? FrameJson(count: 15) : FrameJson(first: generated == 2 ? 'O' : 'Y');
            return Task.FromResult(ModelResponse(frame));
        }));
        using var matrixHttp = new HttpClient(new FakeHandler(async (request, token) =>
        {
            sentGreenValues.Add((await request.Content!.ReadAsByteArrayAsync(token))[1]);
            return new HttpResponseMessage(sentGreenValues.Count == 1
                ? HttpStatusCode.BadRequest
                : HttpStatusCode.NoContent);
        }));
        using var input = new StringReader("Кот\nДом\nМоре");
        using var errors = new StringWriter();
        var session = new DrawingSession(new HttpFrameTransport(matrixHttp), IPAddress.Loopback,
            TextWriter.Null, errors);

        Equal(1, await session.RunAsync(CreateApi(apiHttp), "example-model", input, CancellationToken.None));
        Equal(3, generated);
        Equal("128,255", string.Join(",", sentGreenValues));
        Equal(true, errors.ToString().Contains("16 строк"));
        Equal(true, errors.ToString().Contains("HTTP 400"));
    }),
    ("drawing session shows the palette preview and RGB statistics before sending", async () =>
    {
        using var output = new StringWriter();
        using var matrixHttp = new HttpClient(new FakeHandler((_, _) =>
        {
            var lines = output.ToString().Split(output.NewLine);
            Equal("Превью 16×16:", lines[0]);
            Equal("D...............", lines[1]);
            foreach (var line in lines[2..17])
                Equal("................", line);
            Equal($"Палитра: {FramePalette.Legend}", lines[17]);
            Equal("Ненулевых пикселей: 1/256; максимум RGB: 64/255.", lines[18]);
            Equal(false, output.ToString().Contains("Предупреждение"));
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.NoContent));
        }));
        var session = new DrawingSession(new HttpFrameTransport(matrixHttp), IPAddress.Loopback,
            output, TextWriter.Null);
        await session.SendAsync(RgbFrame.ParseModelJson(FrameJson(first: 'D')), CancellationToken.None);
    }),
    ("drawing session warns about a black frame but still sends it", async () =>
    {
        using var output = new StringWriter();
        var sent = false;
        using var matrixHttp = new HttpClient(new FakeHandler(async (request, token) =>
        {
            Equal(true, output.ToString().Contains("Ненулевых пикселей: 0/256; максимум RGB: 0/255."));
            Equal(true, output.ToString().Contains("Предупреждение: кадр полностью чёрный"));
            var body = await request.Content!.ReadAsByteArrayAsync(token);
            Equal(768, body.Length);
            Equal(true, body.All(value => value == 0));
            sent = true;
            return new HttpResponseMessage(HttpStatusCode.NoContent);
        }));
        var session = new DrawingSession(new HttpFrameTransport(matrixHttp), IPAddress.Loopback,
            output, TextWriter.Null);
        await session.SendAsync(RgbFrame.ParseModelJson(FrameJson()), CancellationToken.None);
        Equal(true, sent);
    })
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

static string FrameJson(int count = 16, char first = '.', char last = '.') =>
    JsonSerializer.Serialize(new
    {
        rows = Enumerable.Range(0, count).Select(i => i == 0 ? first + new string('.', 15) :
            i == count - 1 ? new string('.', 15) + last : new string('.', 16))
    });

static OpenAiApi CreateApi(HttpClient http) =>
    new(http, new Uri("https://example.test/gateway/v1/"), "example-key");

static HttpResponseMessage JsonResponse(string content) =>
    new(HttpStatusCode.OK) { Content = new StringContent(content, Encoding.UTF8, "application/json") };

static HttpResponseMessage ModelResponse(string content) =>
    JsonResponse(JsonSerializer.Serialize(new { choices = new[] { new { message = new { content } } } }));

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
