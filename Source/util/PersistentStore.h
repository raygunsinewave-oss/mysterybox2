// Mystery Box Synthesizer by Raygun
// Global user progression + settings, shared by every plugin instance and DAW.
//   macOS:   ~/Library/Application Support/Raygun/MysteryBoxSynth/
//   Windows: %APPDATA%\Raygun\MysteryBoxSynth\
//   Linux:   ~/.config/Raygun/MysteryBoxSynth/
// Uses juce::PropertiesFile with an InterProcessLock so concurrent instances
// (even in separate host processes) never corrupt the file. Message thread only.
#pragma once

#include <JuceHeader.h>
#include <bitset>

namespace mbx
{
struct UserSettings
{
    int spinCost = 950;          // 250 / 500 / 950 / 1500
    bool freeSpin = false;
    int difficulty = 1;          // 0 Easy, 1 Normal, 2 Hard, 3 Nightmare
    bool midiShooting = false;
    bool mascotEnabled = true;
    bool spinPreview = true;
    bool chimeFollowsKey = true;
    float sfxVolume = 0.7f;
    bool sfxToOutput = false;
    int graphics = 2;            // 0 Low, 1 Medium, 2 High
    int fpsCap = 60;             // 30 / 60
    int uiScalePercent = 100;    // 75..200
    bool gpuRendering = true;
    int polyphonyLimit = 32;
    bool oversampling = false;
    int stealMode = 0;           // 0 oldest, 1 quietest, 2 released first
    float a4 = 440.0f;
    int bendRange = 2;
    bool mpe = false;
    bool fxFollowPreset = true;
};

struct FavoriteEntry
{
    int id = 0;
    juce::int64 addedTime = 0;
};

class PersistentStore : public juce::ChangeBroadcaster,
                        private juce::Timer
{
public:
    static constexpr int kNumInstruments = 2500;

    PersistentStore();
    ~PersistentStore() override;

    // Settings
    const UserSettings& getSettings() const noexcept { return settings; }
    void setSettings (const UserSettings& s);

    // Collection
    bool isDiscovered (int id) const noexcept { return id >= 1 && id <= kNumInstruments && discovered[(size_t) (id - 1)]; }
    int getDiscoveredCount() const noexcept { return (int) discovered.count(); }
    void markDiscovered (int id);

    // Favorites
    bool isFavorite (int id) const noexcept;
    void setFavorite (int id, bool fav);
    const juce::Array<FavoriteEntry>& getFavorites() const noexcept { return favorites; }
    bool exportFavorites (const juce::File& file) const;
    int importFavorites (const juce::File& file); // returns number imported

    // Easter eggs + stats
    bool getFlag (const juce::String& name) const;
    void setFlag (const juce::String& name, bool v);
    juce::int64 getLifetimePoints() const noexcept { return lifetimePoints; }
    void addLifetimePoints (int pts);
    int getLifetimeSpins() const noexcept { return lifetimeSpins; }
    void addLifetimeSpin();

    void resetProgress();
    juce::File getStorageFile() const;

private:
    void load();
    void save();
    void timerCallback() override;

    std::unique_ptr<juce::PropertiesFile> props;
    UserSettings settings;
    std::bitset<kNumInstruments> discovered;
    juce::Array<FavoriteEntry> favorites;
    juce::StringArray flags;
    juce::int64 lifetimePoints = 0;
    int lifetimeSpins = 0;
    juce::Time lastKnownModification;
    bool dirty = false;
};

} // namespace mbx
