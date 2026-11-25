// Mystery Box Synthesizer by Raygun
#pragma once

#include <JuceHeader.h>
#include "ParamIDs.h"
#include "engine/SfxEngine.h"
#include "engine/SynthEngine.h"
#include "game/GameWorld.h"
#include "presets/MysteryBoxRoller.h"
#include "presets/PresetLibrary.h"
#include "util/LockFreeFifo.h"
#include "util/PersistentStore.h"

namespace mbx
{
/** Process-wide, lazily parsed instrument library (shared by all instances). */
class LibraryHolder
{
public:
    const PresetLibrary& get();
private:
    PresetLibrary lib;
    std::once_flag once;
};

/** Builds the wavetable bank on a background thread at plugin load. */
class WavetableBuilder : public juce::Thread
{
public:
    WavetableBuilder() : juce::Thread ("MBX wavetables") { startThread (juce::Thread::Priority::low); }
    ~WavetableBuilder() override { stopThread (10000); }
    void run() override { WavetableBank::get().build(); }
};

struct InventoryItem { int id = 0; bool taken = false; bool overclocked = false; };

class MysteryBoxAudioProcessor : public juce::AudioProcessor,
                                 public GameHost,
                                 private juce::ChangeListener
{
public:
    MysteryBoxAudioProcessor();
    ~MysteryBoxAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return getCurrentInstrumentName(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    // --- Message-thread API -------------------------------------------------
    const PresetLibrary& getLibrary() { return library->get(); }
    PersistentStore& getStore() { return *store; }
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    GameWorld& getGame() { return game; }
    MysteryBoxRoller& getRoller() { return roller; }

    /** Loads an instrument by 1-based id. applyToParams copies the preset's macro/ADSR/filter/FX
        defaults into the host parameters (false when restoring a saved project). */
    void loadInstrument (int id, bool applyToParams, bool overclockedState = false);
    int getCurrentInstrumentId() const noexcept { return currentId.load(); }
    juce::String getCurrentInstrumentName();
    bool isOverclocked() const noexcept { return overclocked.load(); }
    void setOverclocked (bool);

    void startPreview (int id);
    void playSfx (const SfxEvent& e);
    void panic();

    // Points / round (persisted in the project)
    int getPoints() const noexcept { return points.load(); }
    void addPoints (int delta);
    void setPoints (int p) { points = juce::jmax (0, p); }
    juce::Array<InventoryItem> getInventory() const { const juce::ScopedLock sl (stateLock); return inventory; }
    void pushInventory (int id, bool taken);
    void markInventoryTaken (int id);

    /** Most likely key of recently played notes (root 0-11, minor flag). Defaults to C minor. */
    void getChimeKey (int& root, bool& minor);
    /** Returns a MIDI note in the chime key, `degree` steps up the scale from the root (octave 6). */
    int chimeNote (int degree);

    /** Note-ons received from MIDI since the last call (for MIDI-shooting mode). */
    int popMidiShots (float* velocities, int max);

    // Settings snapshot pushed from the global store
    void applySettings (const UserSettings& s);
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }

    // MIDI learn
    void beginMidiLearn (const juce::String& paramId);
    void clearMidiMapping (const juce::String& paramId);
    int getMidiMapping (const juce::String& paramId) const;
    bool isMidiLearning() const noexcept { return learnTarget.load() >= 0; }

    // Meters
    float getCpuLoad() const noexcept { return cpuLoad.load(); }
    int getActiveVoices() const noexcept { return activeVoices.load(); }
    double getHostBpm() const noexcept { return hostBpm.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    float getOutputPeak() const noexcept { return outputPeak.load(); }

    // FX order (message thread writes; audio thread reads packed atomic)
    std::array<int, kNumFx> getFxOrder() const;
    void setFxOrder (const std::array<int, kNumFx>& order);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // GameHost (called by the game on the message thread)
    void gameAddPoints (int pts) override;
    int gamePoints() override { return points.load(); }
    bool gameSpendPoints (int pts) override;
    void gameSfx (Sfx type, float note, float gain, float pan) override;
    int gameChimeNote (int degree) override { return chimeNote (degree); }
    MysteryBoxRoller::Result gameRollBox() override;
    void gameBoxLanded (int presetId) override;
    void gameTakeInstrument (int presetId) override;
    void gameLostInstrument (int presetId) override;
    int gameDisplayPreset (uint32_t seed) override;
    const char* gamePresetName (int presetId) override;
    int gamePresetCategory (int presetId) override;
    int gamePresetRarity (int presetId) override;
    const UserSettings& gameSettings() override { return store->getSettings(); }
    void gameKonami() override;

private:
    struct AudioCommand
    {
        enum Type : uint8_t { LoadPreset, Preview, Sfx, Panic } type = Sfx;
        int presetIndex = -1;
        SfxEvent sfx {};
        int previewNotes[4] {};
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleCommands() noexcept;
    void updateFxParams() noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void processChunk (float* L, float* R, int n) noexcept;
    void renderPreview (float* L, float* R, int n) noexcept;
    void setParam (const juce::String& id, float realValue);

    juce::SharedResourcePointer<LibraryHolder> library;
    juce::SharedResourcePointer<WavetableBuilder> wavetableBuilder;
    juce::SharedResourcePointer<PersistentStore> store;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    GameWorld game;
    MysteryBoxRoller roller;

    // Engine (audio thread)
    SynthEngine engine, previewEngine;
    SfxEngine sfx;
    FxChain fxChain;
    FxParams fxParams;
    LockFreeFifo<AudioCommand, 256> commands;
    LockFreeFifo<float, 128> midiShots;
    juce::HeapBlock<float> scratchL, scratchR, sfxL, sfxR;
    int maxBlock = 0;
    double sampleRate = 48000.0;

    // Preview phrase
    int previewSamplePos = -1;
    int previewNotes[4] {};
    int previewHeld = -1;

    // Cached parameter pointers (read on the audio thread)
    std::atomic<float>* macroParams[8] {};
    std::atomic<float>* overclockParam = nullptr;
    std::atomic<float>* ampParams[4] {};
    std::atomic<float>* cutoffParam = nullptr;
    std::atomic<float>* resParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* panParam = nullptr;
    std::atomic<float>* ambOnParam = nullptr;
    std::atomic<float>* ambLevelParam = nullptr;
    std::atomic<float>* fxOnParams[kNumFx] {};
    std::atomic<float>* fxMixParams[kNumFx] {};
    std::atomic<float>* fxPParams[kNumFx][4] {};
    juce::Array<juce::RangedAudioParameter*> learnableParams;
    LinearSmoother volumeSmoother, panSmoother;

    // Shared state
    std::atomic<int> currentId { 0 };
    std::atomic<bool> overclocked { false };
    std::atomic<int> points { 0 };
    std::atomic<uint64_t> fxOrderPacked { 0x876543210ull };
    std::atomic<float> cpuLoad { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<float> pitchClassEnergy[12] {};
    std::atomic<int> ccMap[128];
    std::atomic<int> learnTarget { -1 };

    // Settings mirrored for the audio thread
    std::atomic<float> sfxVolume { 0.7f };
    std::atomic<bool> sfxToOutput { false }, previewEnabled { true }, oversampling { false }, mpe { false };
    std::atomic<bool> fxFollowPreset { true };
    std::atomic<int> polyLimit { 32 }, stealMode { 0 }, bendRange { 2 };
    std::atomic<float> a4 { 440.0f };
    std::atomic<bool> chimeFollowsKey { true };

    juce::CriticalSection stateLock; // guards inventory (never taken on the audio thread)
    juce::Array<InventoryItem> inventory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MysteryBoxAudioProcessor)
};

} // namespace mbx
