# HyperTune (v0.1)

Plugin auto-tune VST3 pour FL Studio.

## Compiler (Windows)
1. Installer Visual Studio 2022 (charge de travail « Développement Desktop C++ ») et CMake
2. Dans ce dossier :
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release
3. Le .vst3 est copié dans C:\Program Files\Common Files\VST3
4. FL Studio : Options > Manage plugins > Find installed plugins, puis le charger sur une piste du mixeur

## Réglages hyperpop
- Retune Speed 0 ms + Amount 100 % = effet robot dur
- Pitch Shift +3 à +7 st pour une voix pitchée
- Harmony Level + Interval pour les voix empilées
- Glitch Amount pour les sauts d'octave aléatoires
- Drive pour la saturation, Dry/Wet à 100 %
