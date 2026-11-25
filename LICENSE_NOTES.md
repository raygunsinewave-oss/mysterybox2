# Licence notes

## JUCE (important if you sell or close-source this plugin)

This project is built on **JUCE 8**, which is dual-licensed:

- **AGPLv3**: you may use JUCE for free if *your whole product* is released under the AGPLv3, including source
  code for anyone who receives the binaries.
- **JUCE commercial licence** (Starter / Indie / Pro, from juce.com): required if you distribute this plugin
  **closed-source or sell it** without releasing the source under the AGPLv3. Check the current JUCE licence terms for
  revenue limits and tiers before you ship.

**If Mystery Box Synthesizer is sold or distributed closed-source, the owner needs a JUCE commercial licence.**

## Steinberg VST3 / Apple Audio Unit

VST is a registered trademark of Steinberg Media Technologies GmbH. The VST3 SDK is included with JUCE under
Steinberg's licence terms, so review them if you distribute VST3 binaries. Audio Unit support uses Apple's AudioToolbox
frameworks.

## Fonts (bundled in the binary)

All four fonts come from Google Fonts and are licensed under the **SIL Open Font License 1.1**. Full licence texts are
in `Resources/fonts/`:

- Black Ops One — © 2022 The Black-Ops Project Authors
- Cabin Sketch — © 2011 The Cabin Project Authors (Reserved Font Names "Cabin", "Cabin Sketch")
- Share Tech Mono — © 2012 Carrois Type Design, Ralph du Carrois (Reserved Font Name "Share")
- Oswald — © 2016 The Oswald Project Authors

The OFL allows embedding in software. The fonts may not be sold on their own.

## Test-only dependencies

- **Catch2** v3 (Boost Software License 1.0): unit tests only, not shipped in the plugin.
- **pluginval** (Tracktion, GPLv3): downloaded and run by CI only, not shipped.

## Original content / IP

All artwork is drawn procedurally in code and every sound effect is synthesized at runtime. The mystery crate,
"Clank" the tin-robot mascot, "The Overclocker", zombie designs, power-up icons and all instrument names are
original. No assets, names, logos, fonts, sounds or music from any existing game are used. "Raygun" is the
developer's brand name.

## This project's own code

Unless you choose otherwise, the Mystery Box Synthesizer source is © Raygun, all rights reserved. If you distribute
it under the JUCE AGPLv3 option you must license the whole project under the AGPLv3.
