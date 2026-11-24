using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using SmartMatrix.Cli;

Console.OutputEncoding = Encoding.UTF8;
Console.InputEncoding = Encoding.UTF8;

using var cancellation = new CancellationTokenSource();
ConsoleCancelEventHandler cancelHandler = (_, args) =>
{
    args.Cancel = true;
    cancellation.Cancel();
};
Console.CancelKeyPress += cancelHandler;

try
{
    var cancellationToken = cancellation.Token;
    IPAddress address;
    RgbFrame frame;
    if (args is ["--test-frame"])
    {
        address = ReadMatrixAddress();
        frame = RgbFrame.CreateTestPattern();
    }
    else if (args.Length == 0)
    {
        Uri? baseUrl;
        while (!OpenAiApi.TryGetBaseUrl(
            ReadRequired("Базовый URL OpenAI-совместимого API (например, https://api.openai.com/v1): "), out baseUrl))
        {
            Console.WriteLine("Введите абсолютный HTTP(S)-адрес без логина, пароля, параметров и фрагмента.");
        }

        if (baseUrl!.Scheme == Uri.UriSchemeHttp)
            Console.WriteLine("Внимание: по HTTP ключ передаётся без шифрования.");

        var apiKey = ReadApiKey();
        using var http = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false })
        {
            Timeout = TimeSpan.FromMinutes(3),
            MaxResponseContentBufferSize = 4 * 1024 * 1024
        };
        var api = new OpenAiApi(http, baseUrl, apiKey);
        var model = await SelectModelAsync(api, cancellationToken);
        var prompt = ReadRequired("Что нарисовать: ");
        address = ReadMatrixAddress();

        Console.WriteLine("Запрашиваю кадр у модели…");
        var json = await api.GenerateAsync(model, prompt, cancellationToken);
        frame = RgbFrame.ParseModelJson(json);
    }
    else
    {
        Console.Error.WriteLine("Запустите клиент без аргументов или с --test-frame для проверки матрицы.");
        return 1;
    }

    using var matrixHttp = new HttpClient(new HttpClientHandler
    {
        AllowAutoRedirect = false,
        UseProxy = false,
        UseCookies = false
    })
    {
        Timeout = TimeSpan.FromSeconds(10)
    };
    IFrameTransport transport = new HttpFrameTransport(matrixHttp);
    Console.WriteLine($"Отправляю кадр на плату ({address})…");
    await transport.SendAsync(address, frame, cancellationToken);
    Console.WriteLine("Плата подтвердила вывод кадра.");
    return 0;
}
catch (OperationCanceledException)
{
    Console.Error.WriteLine(cancellation.IsCancellationRequested ? "Операция отменена." : "Истекло время ожидания ответа.");
    return 1;
}
catch (EndOfStreamException)
{
    Console.Error.WriteLine("Ввод завершён.");
    return 1;
}
catch (JsonException)
{
    Console.Error.WriteLine("Модель не вернула корректный JSON. Кадр не отправлен.");
    return 1;
}
catch (Exception ex) when (ex is HttpRequestException or FormatException)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}
finally
{
    Console.CancelKeyPress -= cancelHandler;
}

static IPAddress ReadMatrixAddress()
{
    while (true)
    {
        var input = ReadRequired("IPv4-адрес платы (из Serial Monitor): ");
        if (IPAddress.TryParse(input, out var address) && address.AddressFamily == AddressFamily.InterNetwork &&
            address.ToString() == input)
        {
            return address;
        }

        Console.WriteLine("Введите IPv4-адрес, например 192.168.1.42.");
    }
}

static async Task<string> SelectModelAsync(OpenAiApi api, CancellationToken cancellationToken)
{
    string[] models;
    try
    {
        models = await api.ListModelsAsync(cancellationToken);
    }
    catch (HttpRequestException ex) when (ex.StatusCode is not
        (HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden))
    {
        Console.WriteLine(ex.Message);
        models = [];
    }
    catch (FormatException ex)
    {
        Console.WriteLine(ex.Message);
        models = [];
    }

    if (models.Length == 0)
        return ReadRequired("Список моделей недоступен или пуст. Введите ID модели: ");

    for (var i = 0; i < models.Length; i++)
        Console.WriteLine($"{i + 1}. {models[i]}");

    while (true)
    {
        var input = ReadRequired("Номер модели: ");
        if (int.TryParse(input, out var number) && number >= 1 && number <= models.Length)
            return models[number - 1];

        Console.WriteLine($"Введите номер от 1 до {models.Length}.");
    }
}

static string ReadRequired(string prompt)
{
    while (true)
    {
        Console.Write(prompt);
        var input = Console.ReadLine() ?? throw new EndOfStreamException();
        if (!string.IsNullOrWhiteSpace(input))
            return input.Trim();

        Console.WriteLine("Поле не может быть пустым.");
    }
}

static string ReadApiKey()
{
    if (Console.IsInputRedirected)
        return ReadRequired("Ключ API: ");

    while (true)
    {
        Console.Write("Ключ API (ввод скрыт): ");
        var input = new StringBuilder();
        while (true)
        {
            var key = Console.ReadKey(intercept: true);
            if (key.Key == ConsoleKey.Enter)
                break;
            if (key.Key == ConsoleKey.Backspace && input.Length > 0)
                input.Length--;
            else if (!char.IsControl(key.KeyChar))
                input.Append(key.KeyChar);
        }

        Console.WriteLine();
        var value = input.ToString().Trim();
        if (value.Length > 0)
            return value;

        Console.WriteLine("Ключ не может быть пустым.");
    }
}
