# Sigma Q v0.1: EQ with animated dots (VST3)

## Using it
- **Double-click** the canvas to add a band (up to 12). **Drag** a dot to change frequency and gain.
- Every band has its **own colour**. Its shape is drawn in that colour and moves live as the dot moves.
- **Click a dot** to bring its shape to the front and open its card:
  - **Tone** tab: shape, frequency, gain, width, cut slope, stereo placement. A preview shows the band's curve.
  - **Motion** tab: Off / LFO / Orbit, pattern, tempo Sync or free Rate, Sweep and Swell. A live waveform preview shows exactly what the dot is doing right now.
- **Scroll** over a dot for width (Q). **Right-click** for shape / delete. **Delete** key removes the selected band.
- Top bar: Output, Bypass, Motion master switch, Advanced drawer (analyzer, display range, trails, reset).

## Files
- `Dsp.h`: filters + LFO waveforms, shared by audio and UI.
- `PluginProcessor.*`: parameters, motion engine, filtering.
- `EQDisplay.h`: canvas, band shapes, z-order, dots. `BandCard.h`: Tone/Motion card + previews. `PluginEditor.*`: top bar, Advanced drawer.
