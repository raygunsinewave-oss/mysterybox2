#include <cstdlib>
#include <cstdio>
// Mystery Box Synthesizer by Raygun -- headless plugin-level tests
//   mbx_plugin_tests                 run state / stress / real-time tests
//   mbx_plugin_tests --snapshot DIR  also render editor screenshots (needs a display, e.g. Xvfb)
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "util/RealtimeGuard.h"
#include <iostream>
#include <thread>

using namespace mbx;

namespace
{
int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cerr << "FAILED: " #cond " (" __FILE__ ":" << __LINE__ << ")\n"; } } while (0)

void runBlocks (MysteryBoxAudioProcessor& p, int blocks, int blockSize, juce::Random& rng, bool randomMidi, float* peakOut = nullptr)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (randomMidi && rng.nextFloat() < 0.3f)
        {
            const int note = 36 + rng.nextInt (48);
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (40 + rng.nextInt (87))), rng.nextInt (blockSize));
            midi.addEvent (juce::MidiMessage::noteOff (1, 36 + rng.nextInt (48)), rng.nextInt (blockSize));
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rng.nextInt (128)), 0);
        }
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) { ++failures; std::cerr << "non-finite sample\n"; return; }
                peak = std::max (peak, std::abs (v));
            }
    }
    if (peakOut) *peakOut = peak;
}

void testStateRoundTrip()
{
    std::cout << "[state] round trip\n";
    MysteryBoxAudioProcessor a;
    a.prepareToPlay (48000.0, 256);
    a.setPoints (12345);
    a.getRoller().seed (42);
    for (int i = 0; i < 7; ++i) a.getRoller().roll (a.getLibrary(), true);
    a.loadInstrument (1234, true);
    a.pushInventory (1234, true);
    a.pushInventory (77, false);
    a.setOverclocked (true);
    a.setFxOrder ({ 8, 7, 6, 5, 4, 3, 2, 1, 0 });
    a.getGame().round = 9;
    if (auto* prm = a.getAPVTS().getParameter (ids::macro[2])) prm->setValueNotifyingHost (0.77f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    MysteryBoxAudioProcessor b;
    b.prepareToPlay (48000.0, 256);
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getCurrentInstrumentId() == 1234);
    CHECK (b.getPoints() == 12345);
    CHECK (b.isOverclocked());
    CHECK (b.getGame().round == 9);
    CHECK (b.getFxOrder()[0] == 8);
    CHECK (b.getInventory().size() == 2);
    CHECK (std::abs (b.getAPVTS().getRawParameterValue (ids::macro[2])->load() - 0.77f) < 1e-3f);
    // The RNG continues identically: reloading a project is not a free re-roll
    for (int i = 0; i < 25; ++i)
    {
        const auto ra = a.getRoller().roll (a.getLibrary(), true);
        const auto rb = b.getRoller().roll (b.getLibrary(), true);
        CHECK (ra.presetId == rb.presetId);
        CHECK (ra.mascot == rb.mascot);
    }
}

void testAllPresetsLoadInPlugin()
{
    std::cout << "[presets] loading all 2,500 through the processor\n";
    MysteryBoxAudioProcessor p;
    p.prepareToPlay (48000.0, 128);
    juce::Random rng (5);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int id = 1; id <= 2500; id += 1)
    {
        p.loadInstrument (id, true);
        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        if (id % 10 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p.processBlock (buf, midi);
    }
    const double perPreset = (juce::Time::getMillisecondCounterHiRes() - t0) / 2500.0;
    std::cout << "  " << perPreset << " ms per load+block\n";
    CHECK (perPreset < 5.0);
}

void testStress()
{
    std::cout << "[stress] 20 instances, random MIDI, macro automation, rapid preset switching\n";
    constexpr int kInstances = 20;
    juce::OwnedArray<MysteryBoxAudioProcessor> procs;
    for (int i = 0; i < kInstances; ++i)
    {
        auto* p = procs.add (new MysteryBoxAudioProcessor());
        p->prepareToPlay (48000.0, 256);
        p->loadInstrument (1 + i * 120, true);
    }
    juce::Random rng (11);
    const int blocks = 48000 * 10 / 256; // 10 seconds of audio per instance
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kInstances; ++i)
        {
            auto& p = *procs[i];
            if (rng.nextFloat() < 0.002f) p.loadInstrument (1 + rng.nextInt (2500), true); // rapid switching
            if (auto* prm = p.getAPVTS().getParameter (ids::macro[b % 8])) prm->setValueNotifyingHost (rng.nextFloat());
            runBlocks (p, 1, 256, rng, true);
        }
    }
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const double realtimeFactor = secs / 10.0;
    const double perInstance = realtimeFactor / kInstances;
    std::cout << "  20 instances x 10 s rendered in " << secs << " s on ONE thread (" << realtimeFactor * 100.0
              << "% of one core in total, " << perInstance * 100.0 << "% per instance)\n";
   #if defined(NDEBUG)
    CHECK (perInstance < 0.25); // every instance well inside the worst-case budget; hosts spread instances over cores
   #endif
}

void testCpuTypical()
{
    std::cout << "[cpu] typical presets, 8 voices, 48 kHz / 256\n";
    MysteryBoxAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    double worst = 0.0, total = 0.0; int n = 0;
    for (int id : { 1, 101, 301, 601, 901, 1301, 1601, 2101 })
    {
        p.loadInstrument (id, true);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int k = 0; k < 8; ++k) midi.addEvent (juce::MidiMessage::noteOn (1, 48 + k * 3, (juce::uint8) 100), 0);
        p.processBlock (buf, midi);
        midi.clear();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const int blocks = 48000 * 4 / 256;
        for (int b = 0; b < blocks; ++b) { buf.clear(); p.processBlock (buf, midi); }
        const double load = (juce::Time::getMillisecondCounterHiRes() - t0) / 4000.0;
        std::cout << "  #" << id << " " << p.getCurrentInstrumentName() << ": " << load * 100.0 << "% of one core\n";
        worst = std::max (worst, load); total += load; ++n;
        juce::MidiBuffer off; for (int k = 0; k < 128; ++k) off.addEvent (juce::MidiMessage::noteOff (1, k), 0);
        p.processBlock (buf, off);
    }
    std::cout << "  average " << total / n * 100.0 << "%, worst " << worst * 100.0 << "%\n";
}

void testRealtimeSafety()
{
#if MBX_RT_ALLOC_CHECK
    std::cout << "[realtime] allocation tracker active\n";
    MysteryBoxAudioProcessor p;
    p.prepareToPlay (48000.0, 64);
    p.loadInstrument (1500, true);
    juce::Random rng (3);
    runBlocks (p, 200, 64, rng, true); // warm-up (host MidiBuffer growth etc.)
    const int before = rt::violationCount().load();
    runBlocks (p, 4000, 64, rng, true);
    p.loadInstrument (42, true);
    p.startPreview (42);
    p.playSfx ({ Sfx::Gunshot, 60, 1, 0 });
    runBlocks (p, 2000, 64, rng, true);
    const int after = rt::violationCount().load();
    std::cout << "  allocations on the audio thread: " << (after - before) << "\n";
    CHECK (after == before);
#else
    std::cout << "[realtime] allocation tracker compiled out (Release); run a Debug build for this check\n";
#endif
}

void testLimiter()
{
    std::cout << "[safety] output ceiling\n";
    MysteryBoxAudioProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.loadInstrument (1250, true); // a bass
    if (auto* v = p.getAPVTS().getParameter (ids::volume)) v->setValueNotifyingHost (1.0f); // +6 dB
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int k = 0; k < 16; ++k) midi.addEvent (juce::MidiMessage::noteOn (1, 30 + k * 3, (juce::uint8) 127), 0);
    float peak = 0.0f;
    for (int b = 0; b < 200; ++b) { buf.clear(); p.processBlock (buf, midi); midi.clear(); peak = std::max ({ peak, buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512) }); }
    std::cout << "  peak " << juce::Decibels::gainToDecibels (peak) << " dBFS\n";
    CHECK (peak <= juce::Decibels::decibelsToGain (-0.3f) + 1e-5f);
}

template <typename Fn>
void onMsg (Fn&& fn)
{
    juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* f) -> void* { (*static_cast<Fn*> (f))(); return nullptr; }, &fn);
}

void snapshots (const juce::File& dir)
{
    std::cout << "[ui] rendering editor snapshots to " << dir.getFullPathName() << "\n";
    dir.createDirectory();
    std::unique_ptr<MysteryBoxAudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    std::unique_ptr<juce::Component> holder;
    onMsg ([&]
    {
        proc = std::make_unique<MysteryBoxAudioProcessor>();
        auto& p = *proc;
        p.prepareToPlay (48000.0, 256);
        p.setPoints (2340);
        p.loadInstrument (1337, true);
        p.pushInventory (1337, true); p.pushInventory (512, false); p.pushInventory (2401, true); p.pushInventory (77, true);
        auto settings = p.getStore().getSettings();
        settings.gpuRendering = false; // snapshots use the software renderer
        p.getStore().setSettings (settings);
        ed.reset (p.createEditor());
        holder = std::make_unique<juce::Component>();
        holder->setSize (1200, 800);
        holder->addAndMakeVisible (ed.get());
        ed->setBounds (0, 0, 1200, 800);
        holder->setVisible (true); // no desktop window needed: snapshots use the software renderer
    });
    auto& p = *proc;
    auto& game = p.getGame();
    auto pump = [&] (int frames)
    {
        for (int i = 0; i < frames; ++i) { onMsg ([&] { game.update (1.0f / 60.0f); }); juce::Thread::sleep (2); }
    };
    auto shot = [&] (const juce::String& name, float scale = 1.0f)
    {
        onMsg ([&]
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (img, out);
        });
    };
    pump (60 * 4);
    onMsg ([&] { for (int i = 0; i < 4; ++i) { game.aim (300.0f + 40.0f * (float) i, 200.0f); game.shoot(); game.fireCooldown = 0; } });
    pump (40);
    shot ("01_main.png");
    onMsg ([&] { p.setPoints (5000); game.requestSpin(); });
    pump (60 * 2);
    shot ("02_spinning.png");
    pump (60 * 3);
    shot ("03_result.png");
    shot ("03_result_2x.png", 2.0f);
    onMsg ([&] { game.takeResult(); });
    pump (30);
    for (auto [label, file] : { std::pair<const char*, const char*> { "SETTINGS", "04_settings.png" }, { "FX", "05_fx.png" }, { "FAVES", "06_favorites.png" } })
    {
        onMsg ([&, label = juce::String (label)]
        {
            std::function<void (juce::Component*)> find = [&] (juce::Component* c)
            {
                for (auto* ch : c->getChildren())
                {
                    if (auto* btn = dynamic_cast<juce::Button*> (ch); btn != nullptr && btn->getName() == label) { btn->triggerClick(); return; }
                    find (ch);
                }
            };
            find (ed.get());
        });
        juce::Thread::sleep (700);
        shot (file);
    }
    onMsg ([&] { p.setOverclocked (true); p.getStore().setFlag ("egg", true); });
    juce::Thread::sleep (900);
    shot ("07_overclocked.png");
    onMsg ([&] { holder->removeChildComponent (ed.get()); ed.reset(); holder.reset(); proc.reset(); });
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); // progress must survive a CI timeout kill
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI init;
    juce::File snapDir;
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--snapshot") snapDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]);

    std::thread worker ([&]
    {
        std::cout << "[worker] started\n";
        onMsg ([] { std::cout << "[msg-thread] reachable\n"; });
        onMsg ([] { std::cout << "[test] state round trip\n";   testStateRoundTrip(); });
        onMsg ([] { std::cout << "[test] limiter\n";            testLimiter(); });
        onMsg ([] { std::cout << "[test] realtime safety\n";    testRealtimeSafety(); });
        onMsg ([] { std::cout << "[test] all presets\n";        testAllPresetsLoadInPlugin(); });
        onMsg ([] { std::cout << "[test] cpu typical\n";        testCpuTypical(); });
        onMsg ([] { std::cout << "[test] stress\n";             testStress(); });
        if (snapDir != juce::File()) snapshots (snapDir);
        // Exit from here: stopping the Cocoa run loop from a background thread is
        // not reliable on macOS and can leave the process hanging.
        std::cout << (failures == 0 ? "ALL PLUGIN TESTS PASSED\n" : "PLUGIN TESTS FAILED\n") << std::flush;
        std::fflush (nullptr);
        std::_Exit (failures == 0 ? 0 : 1);
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();
    return 1; // not reached
}
