using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;

namespace CircularRingTimer;

public partial class App : Application
{
    private static readonly TimeSpan DefaultDuration = TimeSpan.FromSeconds(10);
    private const string TimerInfoFileName = "timerinfo.txt";

    private void OnStartup(object sender, StartupEventArgs e)
    {
        var startupOptions = ResolveStartupOptions();
        var mainWindow = new MainWindow(startupOptions.Duration, startupOptions.TimerInfoFilePath);
        MainWindow = mainWindow;
        mainWindow.Show();
    }

    private static StartupOptions ResolveStartupOptions()
    {
        var executableDirectory = Path.GetDirectoryName(Environment.ProcessPath) ?? AppContext.BaseDirectory;
        var timerInfoFilePath = Path.Combine(executableDirectory, TimerInfoFileName);
        if (!File.Exists(timerInfoFilePath))
        {
            return new StartupOptions(DefaultDuration, null);
        }

        try
        {
            var firstLine = File.ReadLines(timerInfoFilePath).FirstOrDefault()?.Trim();
            if (double.TryParse(firstLine, NumberStyles.Number, CultureInfo.InvariantCulture, out var milliseconds) &&
                milliseconds > 0)
            {
                return new StartupOptions(TimeSpan.FromMilliseconds(milliseconds), timerInfoFilePath);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }

        return new StartupOptions(DefaultDuration, timerInfoFilePath);
    }

    private sealed record StartupOptions(TimeSpan Duration, string? TimerInfoFilePath);
}
