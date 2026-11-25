# Installing Mystery Box Synthesizer

## macOS (11+, Apple Silicon or Intel)

**Installer:** double-click `macOS/MysteryBoxSynth_Installer.pkg` and choose VST3, AU and/or the standalone app.

**Manual install:**

| File | Copy to |
|---|---|
| `Mystery Box Synthesizer.vst3` | `~/Library/Audio/Plug-Ins/VST3/` (just you) or `/Library/Audio/Plug-Ins/VST3/` (all users) |
| `Mystery Box Synthesizer.component` (AU) | `~/Library/Audio/Plug-Ins/Components/` (or `/Library/Audio/Plug-Ins/Components/`) |
| `Mystery Box Synthesizer.app` | `/Applications/` |

In Finder use **Go → Go to Folder…** and paste the path (`~/Library` is hidden by default).

### Gatekeeper / unsigned builds

If the plugin was built without an Apple Developer ID (no signing secrets in CI), macOS will quarantine it and your DAW
may refuse to load it, or report it as "damaged". Remove the quarantine flag after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Mystery Box Synthesizer.vst3"
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Mystery Box Synthesizer.component"
```

```bash
xattr -dr com.apple.quarantine /Applications/"Mystery Box Synthesizer.app"
```

For the app you can also right-click → **Open** the first time. After installing an AU, restart the AU registry:

```bash
killall -9 AudioComponentRegistrar
```

## Windows (10+, x64)

**Installer:** run `Windows/MysteryBoxSynth_Setup.exe`.

**Manual install:** copy the whole `Mystery Box Synthesizer.vst3` folder to `C:\Program Files\Common Files\VST3\`.
The standalone `Mystery Box Synthesizer.exe` runs from anywhere.

**SmartScreen:** unsigned builds may show "Windows protected your PC". Click **More info → Run anyway**. If your
browser blocked the download, right-click the zip → **Properties** → tick **Unblock** before extracting.

## Rescanning plugins in your DAW

| DAW | How |
|---|---|
| **Ableton Live** | Settings → Plug-Ins → enable *Use VST3 Plug-in System Folders* (and *Use Audio Units* on macOS) → **Rescan** |
| **FL Studio** | Options → Manage plugins → **Find installed plugins** (tick *Verify plugins*), then favourite it in the Plugin database |
| **Logic Pro** (AU) | Logic Pro → Settings → Plug-in Manager → select it → **Reset & Rescan Selection** (it appears under Audio Units → Raygun) |
| **Reaper** | Options → Preferences → Plug-ins → VST → **Re-scan** (or *Clear cache/re-scan*). AU: Plug-ins → AU → re-scan |
| **Studio One** | Studio One → Options → Locations → VST Plug-ins → **Reset Blocklist** / *Scan at startup*, then restart |
| **Bitwig Studio** | Settings → Locations → Plug-in locations → make sure the VST3 folder is enabled → **Rescan** |
| **Cubase / Nuendo** | Studio → VST Plug-in Manager → **Rescan All** (check the *Blocklist* tab if it doesn't appear) |

The plugin is an **instrument**: put it on an instrument/MIDI track. It appears as *Mystery Box Synthesizer* by *Raygun*.

## Where your progress is stored

Favourites, the collection, settings and Easter-egg progress are global (shared by every project and DAW):

- macOS: `~/Library/Application Support/Raygun/MysteryBoxSynth/`
- Windows: `%APPDATA%\Raygun\MysteryBoxSynth\`
- Linux: `~/.config/Raygun/MysteryBoxSynth/`

Points, the current instrument, round and box RNG state are saved inside each project.

## Uninstall

Delete the files listed above (and, if you like, the progress folder). On Windows you can also use *Apps & features*.
