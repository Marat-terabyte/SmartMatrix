using System.Net;
using System.Text.Json;

namespace SmartMatrix.Cli;

internal sealed class DrawingSession(
    IFrameTransport transport, IPAddress address, TextWriter output, TextWriter errors)
{
    public async Task<int> RunAsync(
        OpenAiApi api, string model, TextReader input, CancellationToken cancellationToken)
    {
        var exitCode = 0;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            output.Write("Что нарисовать (Enter — выход): ");
            var prompt = await input.ReadLineAsync(cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            if (string.IsNullOrWhiteSpace(prompt))
                return exitCode;

            try
            {
                output.WriteLine("Запрашиваю кадр у модели…");
                var json = await api.GenerateAsync(model, prompt.Trim(), cancellationToken);
                await SendAsync(RgbFrame.ParseModelJson(json), cancellationToken);
            }
            catch (JsonException)
            {
                errors.WriteLine("Модель не вернула корректный JSON. Кадр не отправлен.");
                exitCode = 1;
            }
            catch (Exception ex) when (ex is HttpRequestException or FormatException)
            {
                errors.WriteLine(ex.Message);
                exitCode = 1;
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                errors.WriteLine("Истекло время ожидания ответа.");
                exitCode = 1;
            }
        }
    }

    public async Task SendAsync(RgbFrame frame, CancellationToken cancellationToken)
    {
        output.WriteLine($"Отправляю кадр на плату ({address})…");
        await transport.SendAsync(address, frame, cancellationToken);
        output.WriteLine("Плата подтвердила вывод кадра.");
    }
}
