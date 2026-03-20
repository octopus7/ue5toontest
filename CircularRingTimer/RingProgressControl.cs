using System;
using System.Windows;
using System.Windows.Media;

namespace CircularRingTimer;

public sealed class RingProgressControl : FrameworkElement
{
    public static readonly DependencyProperty ProgressProperty =
        DependencyProperty.Register(
            nameof(Progress),
            typeof(double),
            typeof(RingProgressControl),
            new FrameworkPropertyMetadata(0d, FrameworkPropertyMetadataOptions.AffectsRender, null, CoerceProgress));

    public static readonly DependencyProperty StrokeThicknessProperty =
        DependencyProperty.Register(
            nameof(StrokeThickness),
            typeof(double),
            typeof(RingProgressControl),
            new FrameworkPropertyMetadata(18d, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty StartAngleProperty =
        DependencyProperty.Register(
            nameof(StartAngle),
            typeof(double),
            typeof(RingProgressControl),
            new FrameworkPropertyMetadata(-90d, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty TrackBrushProperty =
        DependencyProperty.Register(
            nameof(TrackBrush),
            typeof(Brush),
            typeof(RingProgressControl),
            new FrameworkPropertyMetadata(
                new SolidColorBrush(Color.FromArgb(30, 125, 211, 252)),
                FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty ProgressBrushProperty =
        DependencyProperty.Register(
            nameof(ProgressBrush),
            typeof(Brush),
            typeof(RingProgressControl),
            new FrameworkPropertyMetadata(
                new SolidColorBrush(Color.FromArgb(255, 94, 234, 212)),
                FrameworkPropertyMetadataOptions.AffectsRender));

    public double Progress
    {
        get => (double)GetValue(ProgressProperty);
        set => SetValue(ProgressProperty, value);
    }

    public double StrokeThickness
    {
        get => (double)GetValue(StrokeThicknessProperty);
        set => SetValue(StrokeThicknessProperty, value);
    }

    public double StartAngle
    {
        get => (double)GetValue(StartAngleProperty);
        set => SetValue(StartAngleProperty, value);
    }

    public Brush TrackBrush
    {
        get => (Brush)GetValue(TrackBrushProperty);
        set => SetValue(TrackBrushProperty, value);
    }

    public Brush ProgressBrush
    {
        get => (Brush)GetValue(ProgressBrushProperty);
        set => SetValue(ProgressBrushProperty, value);
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);

        var size = RenderSize;
        if (size.Width <= 0 || size.Height <= 0 || StrokeThickness <= 0)
        {
            return;
        }

        var radius = Math.Max(0, (Math.Min(size.Width, size.Height) - StrokeThickness) / 2);
        if (radius <= 0)
        {
            return;
        }

        var center = new Point(size.Width / 2, size.Height / 2);
        var trackPen = CreatePen(TrackBrush, StrokeThickness);
        drawingContext.DrawEllipse(null, trackPen, center, radius, radius);

        if (Progress <= 0)
        {
            return;
        }

        var progressPen = CreatePen(ProgressBrush, StrokeThickness);
        if (Progress >= 0.9999d)
        {
            drawingContext.DrawEllipse(null, progressPen, center, radius, radius);
            return;
        }

        var startPoint = PointOnCircle(center, radius, StartAngle);
        var endPoint = PointOnCircle(center, radius, StartAngle + (Progress * 360d));

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(startPoint, false, false);
            context.ArcTo(
                endPoint,
                new Size(radius, radius),
                0,
                Progress > 0.5d,
                SweepDirection.Clockwise,
                true,
                false);
        }

        geometry.Freeze();
        drawingContext.DrawGeometry(null, progressPen, geometry);
    }

    private static object CoerceProgress(DependencyObject dependencyObject, object baseValue)
    {
        return Math.Clamp((double)baseValue, 0d, 1d);
    }

    private static Pen CreatePen(Brush brush, double thickness)
    {
        return new Pen(brush, thickness)
        {
            StartLineCap = PenLineCap.Round,
            EndLineCap = PenLineCap.Round
        };
    }

    private static Point PointOnCircle(Point center, double radius, double angleInDegrees)
    {
        var angleInRadians = angleInDegrees * (Math.PI / 180d);
        return new Point(
            center.X + (Math.Cos(angleInRadians) * radius),
            center.Y + (Math.Sin(angleInRadians) * radius));
    }
}
