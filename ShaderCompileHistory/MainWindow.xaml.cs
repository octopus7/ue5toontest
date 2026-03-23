using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace ShaderCompileHistory;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private const string TimerInfoFileName = "timerinfo.txt";
    private const string HistoryFileName = "shader-compile-history.txt";
    private const int HistoryBarCount = 10;
    private const double MinimumBarScaleMilliseconds = 60_000d;

    private readonly Stopwatch _stopwatch = new();
    private readonly DispatcherTimer _timer;
    private readonly DispatcherTimer _timerInfoWatchTimer;
    private readonly double _barScaleMilliseconds;
    private readonly string _timerInfoFilePath = Path.Combine(AppContext.BaseDirectory, TimerInfoFileName);
    private bool _hasSeenTimerInfoFile;
    private double _topBarProgress;

    public MainWindow()
    {
        InitializeComponent();

        var historyState = LoadHistoryState();
        _barScaleMilliseconds = historyState.ScaleMilliseconds;
        HistoryBars = historyState.BarProgresses;

        DataContext = this;

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16)
        };
        _timer.Tick += OnTimerTick;

        _hasSeenTimerInfoFile = File.Exists(_timerInfoFilePath);
        _timerInfoWatchTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _timerInfoWatchTimer.Tick += OnTimerInfoWatchTick;

        Loaded += OnLoaded;
        Closed += OnClosed;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<HistoryBarEntry> HistoryBars { get; }

    public string ElapsedText =>
        $"{_stopwatch.Elapsed.TotalSeconds.ToString("0.00", CultureInfo.InvariantCulture)}s";

    public double TopBarProgress
    {
        get => _topBarProgress;
        private set
        {
            if (Math.Abs(_topBarProgress - value) < 0.0001d)
            {
                return;
            }

            _topBarProgress = value;
            OnPropertyChanged();
        }
    }

    private static HistoryState LoadHistoryState()
    {
        var historyFilePath = Path.Combine(AppContext.BaseDirectory, HistoryFileName);
        var timerInfoFilePath = Path.Combine(AppContext.BaseDirectory, TimerInfoFileName);

        try
        {
            if (TryReadDurationMilliseconds(timerInfoFilePath, out var lastDurationMilliseconds))
            {
                File.AppendAllLines(
                    historyFilePath,
                    [lastDurationMilliseconds.ToString("0", CultureInfo.InvariantCulture)]);
            }
        }
        catch (IOException)
        {
            // Ignore file persistence issues and keep the UI responsive.
        }
        catch (UnauthorizedAccessException)
        {
            // Ignore file persistence issues and keep the UI responsive.
        }

        var historyDurations = ReadHistoryDurations(historyFilePath);
        var recentDurations = historyDurations
            .TakeLast(HistoryBarCount)
            .Reverse()
            .ToArray();
        var scaleMilliseconds = Math.Max(
            recentDurations.DefaultIfEmpty(MinimumBarScaleMilliseconds).Max(),
            MinimumBarScaleMilliseconds);

        return new HistoryState(
            scaleMilliseconds,
            CreateHistoryBarProgresses(recentDurations, scaleMilliseconds));
    }

    private static IReadOnlyList<double> ReadHistoryDurations(string historyFilePath)
    {
        if (!File.Exists(historyFilePath))
        {
            return [];
        }

        try
        {
            return File.ReadLines(historyFilePath)
                .Select(ParseDurationMilliseconds)
                .Where(durationMilliseconds => durationMilliseconds.HasValue)
                .Select(durationMilliseconds => durationMilliseconds!.Value)
                .Where(durationMilliseconds => durationMilliseconds > 0d)
                .ToArray();
        }
        catch (IOException)
        {
            return [];
        }
        catch (UnauthorizedAccessException)
        {
            return [];
        }
    }

    private static double? ParseDurationMilliseconds(string rawLine)
    {
        var value = rawLine.Trim();

        if (!double.TryParse(
                value,
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var durationMilliseconds))
        {
            return null;
        }

        return durationMilliseconds;
    }

    private static bool TryReadDurationMilliseconds(string timerInfoFilePath, out double durationMilliseconds)
    {
        durationMilliseconds = 0d;

        if (!File.Exists(timerInfoFilePath))
        {
            return false;
        }

        var rawValue = File.ReadAllText(timerInfoFilePath).Trim();
        var parsedDuration = ParseDurationMilliseconds(rawValue);

        if (!parsedDuration.HasValue || parsedDuration.Value <= 0d)
        {
            return false;
        }

        durationMilliseconds = parsedDuration.Value;
        return true;
    }

    private static IReadOnlyList<HistoryBarEntry> CreateHistoryBarProgresses(
        IReadOnlyList<double> recentDurations,
        double scaleMilliseconds)
    {
        var barProgresses = new HistoryBarEntry[recentDurations.Count];

        for (var index = 0; index < recentDurations.Count; index++)
        {
            var durationMilliseconds = recentDurations[index];
            barProgresses[index] = new HistoryBarEntry(
                Math.Clamp(durationMilliseconds / scaleMilliseconds, 0d, 1d),
                (int)Math.Floor(durationMilliseconds / 1000d));
        }

        return barProgresses;
    }

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        UpdateViewState();
        _stopwatch.Start();
        _timer.Start();
        _timerInfoWatchTimer.Start();
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        _timer.Stop();
        _timerInfoWatchTimer.Stop();
        _stopwatch.Stop();
    }

    private void OnTimerTick(object? sender, EventArgs e)
    {
        UpdateViewState();
    }

    private void OnTimerInfoWatchTick(object? sender, EventArgs e)
    {
        var timerInfoFileExists = File.Exists(_timerInfoFilePath);

        if (_hasSeenTimerInfoFile && !timerInfoFileExists)
        {
            Close();
            return;
        }

        _hasSeenTimerInfoFile |= timerInfoFileExists;
    }

    private void UpdateViewState()
    {
        // The top bar grows against the recent-history scale so current and past runs share one frame of reference.
        TopBarProgress = Math.Clamp(
            _stopwatch.Elapsed.TotalMilliseconds / _barScaleMilliseconds,
            0d,
            1d);

        OnPropertyChanged(nameof(ElapsedText));
    }

    private void HandleDragMove(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is DependencyObject source &&
            FindVisualParent<Button>(source) is not null)
        {
            return;
        }

        if (e.LeftButton == MouseButtonState.Pressed)
        {
            DragMove();
        }
    }

    private void CloseButton_OnClick(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private static T? FindVisualParent<T>(DependencyObject? child)
        where T : DependencyObject
    {
        while (child is not null)
        {
            if (child is T typedChild)
            {
                return typedChild;
            }

            child = VisualTreeHelper.GetParent(child);
        }

        return null;
    }

    private sealed record HistoryState(double ScaleMilliseconds, IReadOnlyList<HistoryBarEntry> BarProgresses);

    public sealed record HistoryBarEntry(double Progress, int Seconds)
    {
        public string SecondsLabel => $"{Seconds}s";
    }
}
