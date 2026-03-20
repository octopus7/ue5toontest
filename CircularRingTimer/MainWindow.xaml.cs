using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;

namespace CircularRingTimer;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private readonly TimeSpan _countdownDuration;
    private readonly string? _timerInfoFilePath;
    private readonly Stopwatch _stopwatch = new();
    private readonly DispatcherTimer _frameTimer;
    private readonly DispatcherTimer _fileProbeTimer;
    private bool _isClosing;
    private double _progress;
    private TimeSpan _remainingDuration;

    public MainWindow(TimeSpan countdownDuration, string? timerInfoFilePath)
    {
        InitializeComponent();
        DataContext = this;

        _countdownDuration = countdownDuration;
        _timerInfoFilePath = timerInfoFilePath;
        _remainingDuration = countdownDuration;

        _frameTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16)
        };
        _frameTimer.Tick += OnFrameTick;

        _fileProbeTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _fileProbeTimer.Tick += OnFileProbeTick;

        Loaded += OnLoaded;
        Closed += OnClosed;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public double Progress
    {
        get => _progress;
        private set
        {
            if (Math.Abs(_progress - value) < 0.0001d)
            {
                return;
            }

            _progress = value;
            OnPropertyChanged();
        }
    }

    public string RemainingTimeText => _remainingDuration.ToString(@"mm\:ss\.ff");

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        UpdateTimerState();
        _stopwatch.Start();
        _frameTimer.Start();

        if (_timerInfoFilePath is not null)
        {
            _fileProbeTimer.Start();
        }
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        _frameTimer.Stop();
        _fileProbeTimer.Stop();
        _stopwatch.Stop();
    }

    private void OnFrameTick(object? sender, EventArgs e)
    {
        UpdateTimerState();
    }

    private void UpdateTimerState()
    {
        var totalSeconds = _countdownDuration.TotalSeconds;
        var elapsedSeconds = _stopwatch.Elapsed.TotalSeconds;

        if (elapsedSeconds >= totalSeconds)
        {
            Progress = 1d;
            _remainingDuration = TimeSpan.Zero;
            OnPropertyChanged(nameof(RemainingTimeText));
            CloseWindow();
            return;
        }

        Progress = Math.Clamp(elapsedSeconds / totalSeconds, 0d, 1d);
        _remainingDuration = _countdownDuration - TimeSpan.FromSeconds(elapsedSeconds);
        OnPropertyChanged(nameof(RemainingTimeText));
    }

    private void OnFileProbeTick(object? sender, EventArgs e)
    {
        if (_timerInfoFilePath is null)
        {
            return;
        }

        if (!File.Exists(_timerInfoFilePath))
        {
            CloseWindow();
        }
    }

    private void HandleDragMove(object sender, MouseButtonEventArgs e)
    {
        if (e.LeftButton == MouseButtonState.Pressed)
        {
            DragMove();
        }
    }

    private void Window_OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            CloseWindow();
        }
    }

    private void CloseWindow()
    {
        if (_isClosing)
        {
            return;
        }

        _isClosing = true;
        _frameTimer.Stop();
        _fileProbeTimer.Stop();
        _stopwatch.Stop();
        Close();
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
