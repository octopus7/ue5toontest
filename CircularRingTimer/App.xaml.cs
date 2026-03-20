using System;
using System.Globalization;
using System.Windows;

namespace CircularRingTimer;

public partial class App : Application
{
    private static readonly TimeSpan DefaultDuration = TimeSpan.FromSeconds(10);

    private void OnStartup(object sender, StartupEventArgs e)
    {
        var duration = ResolveDuration(e.Args);
        var mainWindow = new MainWindow(duration);
        MainWindow = mainWindow;
        mainWindow.Show();
    }

    private static TimeSpan ResolveDuration(string[] args)
    {
        if (args.Length == 0)
        {
            return DefaultDuration;
        }

        if (double.TryParse(args[0], NumberStyles.Number, CultureInfo.InvariantCulture, out var milliseconds) &&
            milliseconds > 0)
        {
            return TimeSpan.FromMilliseconds(milliseconds);
        }

        return DefaultDuration;
    }
}
