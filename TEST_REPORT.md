# Validation

```powershell
./build.ps1
./test-windows.ps1
# Optional real-time performance run:
./test-windows.ps1 -SoakSeconds 30
```

Core tests cover configuration, audio conversion, FFT and bands, smoothing, timing, scrolling, camera motion, gravity, rebound and fixed lifetime color. The executable reports each assertion; CTest wraps the suite as one test.

The Windows integration harness needs an interactive desktop and hardware Direct3D 11 GPU. It checks CPU/GPU particle agreement, palette weights, full-yellow output, additive overlap, composition transparency, window modes, renderer recreation and relevant settings. Reports and screenshots go to `test-output`, excluded from Git and release archives.

GitHub Actions runs the Release build and core suite. Hosted CI does not substitute for hardware GPU and desktop checks. Performance depends on hardware and settings. Release-specific validation results appear in release notes.
