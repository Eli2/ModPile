# Known issues

## Slight click when toggling the equalizer

OpenAL exposes the equalized and direct audio routes as separate source
properties, so ModPile cannot switch them with one operation. The paths may
therefore briefly overlap or leave a short gap while enabling or disabling the
equalizer. This can produce a slight click, particularly when toggling it
rapidly.

This does not otherwise affect playback or equalizer operation.
