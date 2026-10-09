# Sigma-Q stress audit

Source reviewed: [Sigma-Q at `b51ac8e`](https://github.com/KoneLabie/Sigma-Q/tree/b51ac8e82094f58031fa6b558fbdf288aa4dea42). Production source was not edited. Tests ran on Windows with MSVC 19.51. This directory contains the source harnesses, the response plot, and sanitized pluginval logs. Results describe that commit, not later changes.

## Build and host validation

The C++17/JUCE 8.0.4 project built Release VST3 and standalone targets. A separate VST3 factory smoke test loaded the module, initialized it, found three classes, created its audio component, and unloaded cleanly.

The built VST3 was tested with [pluginval v1.0.4](https://github.com/Tracktion/pluginval/releases/tag/v1.0.4):

| Run | Result | Scope |
| --- | --- | --- |
| [Level 5](pluginval/level5.txt) | Pass | Processing and automation across 44.1, 48, and 96 kHz and block sizes 16–1024 |
| [Level 7 with editor](pluginval/level7_gui.txt) | Pass | Editor open/close, editor while processing, automation, background state, parameter thread safety |
| [Ordered level 10](pluginval/level10_ordered.txt) | Pass | Strict tests at 44.1 kHz / 64 samples |
| [Randomized level 10](pluginval/level10.txt) | Fail | 22 two-state parameter restoration comparisons after parameter fuzzing; audio processing completed at 22.05–192 kHz and 1–4096 sample blocks |

The randomized failure is order dependent: the ordered level-10 and level-6 state restoration runs passed. Every failed comparison involved an on/off parameter whose expected normalized value was `0.87695`, between its two valid discrete states. The plugin's saved on/off state is quantized, so the validator's comparison may not represent ordinary preset restoration. The exact host behavior after arbitrary fractional automation values needs a targeted follow-up before classifying it as a user-visible preset bug. These pluginval runs did not include Steinberg's separate VST3 validator.

## DSP sweep

The [native C++ harness](native_dsp_stress/sigmaq_dsp_stress.cpp) includes the repository's `Source/Dsp.h` and exercises its actual filter implementation. It checked 9,072 configurations across seven filter types, 22.05–192 kHz, extreme frequency/Q/gain values, and all cut slopes. No coefficient or response was nonfinite and no tested pole reached radius 1. In 1,188 sine-response comparisons, measured response matched the predicted display curve within 0.0077 dB whenever the curve was above -80 dB. The [response plot](native_dsp_stress/sigmaq_dsp_response.png) shows representative extreme bell and shelf settings; its [CSV](native_dsp_stress/sigmaq_dsp_response.csv) is included.

To rebuild the native harness, open an MSVC developer shell in `tests/stress/native_dsp_stress` and run:

```powershell
cl /nologo /O2 /EHsc /std:c++17 /I ..\..\..\Source sigmaq_dsp_stress.cpp /Fe:sigmaq_dsp_stress.exe
.\sigmaq_dsp_stress.exe
.\sigmaq_dsp_stress.exe plot > sigmaq_dsp_response.csv
python plot_response.py
```

The plot script needs Pillow. On a compiler with C++17 support, the C++ harness can also be built with `c++ -O2 -std=c++17 -I ../../../Source sigmaq_dsp_stress.cpp -o sigmaq_dsp_stress`. The separate `dsp_stress.mjs` runs with Node.js from `tests/stress`.

The isolated 12-band stereo DSP benchmark, including 32-sample coefficient refresh, used approximately 3–4 ms per audio second for moving bells at 44.1 kHz and 15–16 ms for moving 48 dB/oct cuts. At 192 kHz those ranges were roughly 14–16 ms and 65–69 ms. These are warm-cache core-only timings; JUCE, the analyzer, UI, operating-system scheduling, and a DAW were not included.

## Reproducible weak spots

1. **Motion discontinuity:** At 44.1 kHz, a 10 Hz Saw LFO on one resonant low shelf generated a 0.780 one-sample output jump from a 0.1 DC input at the cycle wrap. Continuous Sine motion also produced a 0.030 jump at a 32-sample coefficient update under the same extreme settings. The processor applies motion to filter coefficients without interpolation within the subblock. See [Dsp.h](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/Dsp.h#L116-L135) and [PluginProcessor.cpp](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/PluginProcessor.cpp#L207-L253).
2. **Bypass freezes filter memory:** A [source-math reproduction](dsp_stress.mjs) fed a 20 Hz, 24 dB/oct high-cut with a constant signal, bypassed it through a second of zero input, then re-enabled it. The first resumed output sample was approximately 1.0 despite zero input and remained about 0.488 after 1,000 samples. The processor skips its filter loop while bypassed. This needs confirmation through the compiled plugin. See [PluginProcessor.cpp](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/PluginProcessor.cpp#L145-L159).
3. **Extreme gain stacking:** Twelve aligned +30 dB, Q18 shelves with +18 dB output produced a finite measured peak of about `6.29e33` (676 dBFS) from a full-scale sine. This allowed configuration can overwhelm downstream audio, despite the filters themselves remaining mathematically stable.
4. **Analyzer thread race:** The audio thread [writes plain float buffers](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/PluginProcessor.cpp#L166-L174) while the UI [reads them](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/EQDisplay.h#L395-L404). The atomic write index does not synchronize those data accesses. This is a source-level finding; no crash or sanitizer result was observed.
5. **Mono placement:** The plugin accepts mono layouts, but the [mono processing branch](https://github.com/KoneLabie/Sigma-Q/blob/b51ac8e82094f58031fa6b558fbdf288aa4dea42/Source/PluginProcessor.cpp#L270-L287) filters the only channel without consulting the selected stereo placement. Thus `Right only` and `Side only` still affect mono audio. This was confirmed by source inspection, not by a separate host-output assertion.

## Limits

No manual DAW listening session, audio-interface latency test, macOS AU test, Steinberg VST3 validator, or thread sanitizer run was completed. The bypass and motion tests reproduce source DSP behavior; only pluginval exercised the compiled VST3. The project is an EQ effect and does not receive MIDI notes as a VSTi. These harnesses are not wired into the repository's Windows build workflow.
