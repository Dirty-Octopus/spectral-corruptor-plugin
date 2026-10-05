# Harmonic effects

These effects take inspiration from the workflows described in the local DtBlkFx manual and `HarmMatchProcess` / `HarmMatchFx` source. The DSP here was written independently; it does not use DtBlkFx's waveform coefficient tables or promise matching presets/audio. In particular, Pointy and Sweep use the analytic envelopes below.

## Harmonic Match

Begin with a clearly pitched single note, voice or bass. Set Amount around 30–60%; compare Input and Output. Auto Track estimates the strongest peak between 40 and 2000 Hz (bounded by sample rate / 8), interpolates it, and smooths nearby estimates. Strong overtones can be mistaken for the fundamental. Disable Auto Track and enter the fundamental manually when this happens. Chords, noisy attacks and unpitched audio will not track reliably.

| Shape | Envelope at Colour = 50% | Expected character |
|---|---|---|
| Triangle | Odd partials, amplitude 1/n² | Softer, hollow, fewer high partials |
| Square | Odd partials, amplitude 1/n | Hollow with stronger upper harmonics |
| Saw | All partials, amplitude 1/n | Brighter, fuller harmonic stack |
| Pointy | All partials, amplitude 1/√n | Dense, bright upper spectrum |
| Sweep | Gaussian emphasis moving across harmonics 1–32 | Moving resonant/formant-like focus |

For Triangle/Square/Saw/Pointy, Colour tilts the harmonic envelope. For Sweep it moves the emphasized harmonic. Width defines the region around each harmonic; Amount blends the transformation. The target is energy-normalized, with bounded per-band gain.

**Scale** redistributes energy in existing harmonic bands and preserves their phases; silent bands stay silent. **Resynth** copies the fundamental band into the target harmonic regions, filling missing partials. This is a spectral texture process, not a phase-vocoder-quality pitch shifter. It may deliberately sound rough on transients or changing pitches. Silent input stays silent in both modes.

## Harmonic Sculpt

Auto/manual fundamental tracking as above. Odd Harmonics and Even Harmonics separately set gain (−48 to +12 dB); Width controls how close to each harmonic processing reaches. Amount = 0 is transparent.

A useful starting point is Amount 60%, Odd 0 dB, Even −18 dB. Listen for a hollowing of a monophonic sound. Manual fundamental selection can act as a fixed spectral comb.

## Spectral Contrast

Positive Contrast emphasizes stronger frequency components relative to quieter ones; negative values flatten the distribution. Energy compensation limits large loudness changes and the per-bin gain is bounded. This is spectral dynamics, not a time-domain compressor. Zero is transparent; silent bins are not filled with generated noise.

## Frequency Shift

Shift translates frequency by a constant Hz offset, with fractional-bin interpolation and per-hop phase advance. It changes harmonic relationships: 200 and 400 Hz shifted +100 become 300 and 500 Hz. This differs from a musical transposition that preserves frequency ratios. Zero is transparent. Material shifted outside the valid spectral interior is discarded; DC/Nyquist endpoints stay protected.

## Shared stage controls

Low/High select the output bins affected by a stage. Reversing Low and High selects the outside region. Stage Mix blends that effect before subsequent stages; Master Dry/Wet blends the complete chain. Range defaults preserve existing presets. Spectrograms highlight the selection and currently display the left/mono channel.

Sound descriptions above are expected behavior from the algorithms. Automated tests establish numerical behavior and safety properties; final musical evaluation is by listening to your material.
