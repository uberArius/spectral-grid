# Architecture

## Data flow

```text
WASAPI loopback → PCM/float conversion and downmix → Hann-window FFT
 → logarithmic bands → gate/gain/compression/optional smoothing
 → timestamped SPSC spectrum queue → timeline → column ring buffer
 → RGBA32F particle state → instanced HLSL billboards
 → Direct3D 11 alpha swap chain → DirectComposition
```

| Module | Responsibility |
| --- | --- |
| Application | Message loop, frame pacing, orchestration, GPU recovery and integration harness |
| WindowManager | Win32 placement, fullscreen, borderless and topmost modes |
| AudioCapture / AudioSamples | WASAPI worker, reconnection, sample conversion and downmix |
| SpectrumAnalyzer / SpectrumTimeline | FFT, band integration, processing and timestamped lookup |
| GridSimulation | History ring, column advancement, particle flight and dirty rows |
| Renderer | D3D11, DirectComposition, GPU uploads, shaders and test readback |
| Camera | Quaternion rotation, local-plane spin, tilt, pan, zoom and inertia |
| SettingsUI | Dear ImGui panel and diagnostics |
| ConfigManager | Defaults, validation and atomic JSON persistence |

## Timing and audio

Audio and rendering run independently. The FFT hop is `min(fftSize / 4, sampleRate / 480)`, with a lower bound of one sample. Logarithmic bands integrate a piecewise-linear power spectrum. Closely spaced bass bands can remain correlated because FFT resolution is finite.

The SPSC queue has one producer and one consumer. Overflow drops new spectra and increments diagnostics. Settings are copied under a short lock; FFT work happens outside it.

Without smoothing, analysis skips temporal attack/release blending and the timeline does not interpolate successive spectra. FFT windows still overlap. With smoothing enabled, the timeline can interpolate with latency.

The active column follows audio until its history boundary. Completed columns retain independent targets. Fractional scroll progress and QueryPerformanceCounter keep motion independent of frame rate.

## Particle flight and color

Target height `H` launches a particle at `0.7H` with upward speed `sqrt(2g × 0.3H)`. The GPU evaluates its analytic trajectory. At first ground contact it may bounce to `bounce × H` (default 0.4). After the second landing it rests at zero. Zero bounce disables rebound.

Gravity changes rebase position and velocity using the old acceleration before starting the next trajectory. Periodic rebasing limits floating-point time values. Bounce changes do not restart an active rebound.

Each texture entry stores height, signed downward velocity, reference time and signed color height. The last magnitude retains the original palette target; its sign tracks whether the first impact was consumed. Color does not fade as a particle descends. The palette power curve maps the chosen midpoint height to half the transition.

Only dirty state rows and frame constants are uploaded. Instanced billboards use additive premultiplied blending. Background transparency is independent of particle brightness. Staging textures and readback are used by tests, not ordinary rendering.

## Persistence and recovery

Settings are validated and saved through a temporary file and atomic replacement. Audio reconnects after endpoint changes. GPU recovery recreates resources and uploads retained history. Resizing happens outside the window procedure. Tests use isolated settings.
