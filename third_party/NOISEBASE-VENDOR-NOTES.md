# NoiseBase dependency packaging

JUCE is vendored from version 9.0.6. The Windows NoiseBase build uses the framework modules and CMake support; it does not build the Android demo applications.

The following unused Android Firebase configuration files are intentionally omitted because they contain JUCE's upstream demo Google API key:

- `JUCE/examples/Assets/google-services.json`
- `JUCE/examples/DemoRunner/Builds/Android/app/src/main/assets/google-services.json`
- `JUCE/extras/AudioPluginHost/Builds/Android/app/src/main/assets/google-services.json`

NoiseBase does not use Firebase, Google APIs, or these demo credentials. Omitting these files does not change the Windows application or VST3 audio processing. Upstream licenses remain intact. Building JUCE's Android push-notification demos would require your own Firebase configuration.

Publication checks must include all vendored dependencies and nested source archives, not only first-party code. Use Gitleaks with full redaction and archive scanning before publishing.
