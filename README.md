# Alfred

*A simple Denoiser at your Service* — by [dmajorsound](https://dmajorsound.com)

Alfred splits a recording into **Signal**, **Noise** and **Reverb** and lets you rebalance them.
Built as VST3, AU (macOS) and CLAP.

## How the code is organized

| Path | What it is |
|---|---|
| `jsfx/Alfred.jsfx` | The plug-in itself, as a REAPER JSFX. This is the source of truth for sound and interface. |
| `tools/eel2cpp.py` | Translates the JSFX into C++ with the exact arithmetic of REAPER's EEL2. |
| `src/gen/` | The translated code. Generated, never edited by hand. |
| `src/core/` | Runtime for the translated code (memory, FFT from WDL, strings, project state). |
| `src/plugin/` | JUCE plug-in: parameters, latency, state, and the interface drawn with JUCE. |
| `tests/` | Null test against the JSFX and a headless interface test. |

To change Alfred: edit `jsfx/Alfred.jsfx`, run `python3 tools/eel2cpp.py jsfx/Alfred.jsfx src/gen AlfredCore`,
commit. GitHub builds the plug-ins for macOS and Windows (Actions tab, artifacts of the latest run).

## Build locally

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Third-party code

- [JUCE](https://juce.com) (fetched at build time)
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (fetched at build time)
- `src/core/fft.c`, `src/core/fft.h`: WDL FFT, Copyright (C) 2006 and later Cockos Incorporated, based on DJBFFT — see the license in the file headers.
