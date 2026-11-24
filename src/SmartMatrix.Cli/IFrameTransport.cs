using System.Net;

namespace SmartMatrix.Cli;

internal interface IFrameTransport
{
    Task SendAsync(IPAddress address, RgbFrame frame, CancellationToken cancellationToken);
}
