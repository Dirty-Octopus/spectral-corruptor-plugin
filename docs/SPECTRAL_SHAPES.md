# Spectral Mirror, Bloom and Comb

Independent, deterministic spectral designs. Each starts at Amount/Depth = 0 for transparent insertion. The shared Low / High and Stage Mix controls work as with the other spectral modules. Low > High processes the outside region.

## Spectral Mirror

Maps input frequency **f** to **2 × Mirror Centre − f**. At 100% Amount, a 1.5 kHz tone with a 1.6875 kHz centre moves to 1.875 kHz. Material mapping below DC or above Nyquist disappears. This is linear frequency reflection, not a musical pitch inversion. Fractional bins interpolate; conjugation and per-channel hop phase produce the reflected progression. Expected character: reversed spectral contour and inharmonic metallic tones.

## Spectral Bloom

A frequency-domain power-envelope blur, distinct from Temporal Smear's averaging of previous frames. Spread is the neighbourhood radius in Hz. A linear-time prefix sum averages power; total interior-bin power is conserved, then Amount interpolates the power envelopes. Occupied-bin phases are retained. Newly occupied bins use deterministic bin-frequency oscillators. No feedback or new random source; silence remains silence. Expected character: widening, softened partials and a diffuse halo. It is not an acoustic reverb, and power conservation does not guarantee unchanged perceived loudness or peak amplitude.

## Spectral Comb

A periodic spectral gate. Tooth Spacing is the frequency period in Hz. Offset moves the pattern by a percentage of that period; Tooth Width opens or closes each pass region. Soft tooth edges avoid an abrupt binary mask. Depth blends from unity to the gate; it never boosts individual bins. This is a spectral mask, not a feedback-delay comb. Expected character: hollow, pitched filtering or sharply segmented textures.

## Stability and cost

Mirror and Bloom allocate scratch space only in `prepare`; Comb needs no scratch allocation. Every frame is bounded by the prepared bin count, with explicit DC/Nyquist exclusion. Work is O(number of bins), including Bloom at large Spread. Both channels have independent phase clocks. Tests cover silence, known-bin behaviour, power bounds, 22.05–192 kHz rates, 513–16,385 bins, parameter extremes and FFT reconfiguration. Mirror is also checked on actual tones through the full plugin processor.
