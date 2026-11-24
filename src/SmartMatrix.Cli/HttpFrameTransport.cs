using System.Net;
using System.Net.Http.Headers;

namespace SmartMatrix.Cli;

internal sealed class HttpFrameTransport(HttpClient http) : IFrameTransport
{
    public async Task SendAsync(IPAddress address, RgbFrame frame, CancellationToken cancellationToken)
    {
        var uri = new UriBuilder(Uri.UriSchemeHttp, address.ToString(), 80, "/frame").Uri;
        using var request = new HttpRequestMessage(HttpMethod.Post, uri)
        {
            Content = new ByteArrayContent(Encode(frame))
        };
        request.Content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");

        using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        if (response.StatusCode != HttpStatusCode.NoContent)
        {
            throw new HttpRequestException(
                $"Плата ({address}): ожидался HTTP 204, получен HTTP {(int)response.StatusCode}.",
                inner: null, response.StatusCode);
        }
    }

    private static byte[] Encode(RgbFrame frame)
    {
        var bytes = new byte[RgbFrame.PixelCount * 3];
        var offset = 0;
        foreach (var pixel in frame.Pixels)
        {
            bytes[offset++] = pixel.R;
            bytes[offset++] = pixel.G;
            bytes[offset++] = pixel.B;
        }
        return bytes;
    }
}
