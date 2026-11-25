#include <cstdlib>
// Mystery Box Synthesizer by Raygun
// Minimal headless VST3 host used to smoke-test the BUILT plugin binary
// (complements pluginval, which runs in CI):
//   mbx_host_check "<path to Mystery Box Synthesizer.vst3>"
#include <JuceHeader.h>
#include <iostream>
#include <thread>

namespace
{
int failures = 0;
#define CHECK(c) do { if (! (c)) { ++failures; std::cerr << "FAILED: " #c "\n"; } } while (0)

template <typename Fn>
void onMsg (Fn&& fn)
{
    juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* f) -> void* { (*static_cast<Fn*> (f))(); return nullptr; }, &fn);
}

void run (const juce::String& path)
{
    juce::AudioPluginFormatManager fm;
    juce::OwnedArray<juce::PluginDescription> found;
    std::unique_ptr<juce::AudioPluginInstance> inst;
    juce::VST3PluginFormat vst3;
    onMsg ([&] { vst3.findAllTypesForFile (found, path); });
    std::cout << "plugins found in bundle: " << found.size() << "\n";
    CHECK (found.size() == 1);
    if (found.isEmpty()) return;
    const auto& d = *found[0];
    std::cout << "  name=" << d.name << " manufacturer=" << d.manufacturerName << " category=" << d.category
              << " isInstrument=" << d.isInstrument << " version=" << d.version << "\n";
    CHECK (d.name == "Mystery Box Synthesizer");
    CHECK (d.manufacturerName == "Raygun");
    CHECK (d.isInstrument);

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int bs : { 16, 32, 256, 4096 })
        {
            juce::String err;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            onMsg ([&] { inst = vst3.createInstanceFromDescription (d, sr, bs, err); });
            const double createMs = juce::Time::getMillisecondCounterHiRes() - t0;
            CHECK (inst != nullptr);
            if (inst == nullptr) { std::cerr << err << "\n"; return; }
            inst->enableAllBuses();
            inst->prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs);
            juce::MidiBuffer midi;
            float peak = 0.0f; bool finite = true;
            const int blocks = (int) (sr * 1.5) / bs;
            for (int b = 0; b < blocks; ++b)
            {
                midi.clear();
                if (b == 0) for (int n : { 48, 55, 60, 64 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 120), 0);
                if (b == blocks / 2) for (int n : { 48, 55, 60, 64 }) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                buf.clear();
                inst->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float v = buf.getSample (c, i); finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            }
            CHECK (finite);
            CHECK (peak <= 0.9661f + 1e-4f);
            CHECK (inst->getLatencySamples() == 0);
            std::cout << "  sr=" << sr << " block=" << bs << " instantiate=" << juce::String (createMs, 1) << " ms peak=" << juce::Decibels::gainToDecibels (peak) << " dBFS\n";
            // State round trip
            juce::MemoryBlock st;
            inst->getStateInformation (st);
            inst->setStateInformation (st.getData(), (int) st.getSize());
            juce::MemoryBlock st2;
            inst->getStateInformation (st2);
            CHECK (st2.getSize() > 100);
            inst->releaseResources();
            onMsg ([&] { inst.reset(); });
        }

    // Editor open time
    juce::String err;
    onMsg ([&] { inst = vst3.createInstanceFromDescription (d, 48000.0, 256, err); });
    double editorMs = 0.0;
    onMsg ([&]
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        editorMs = juce::Time::getMillisecondCounterHiRes() - t0;
        CHECK (ed != nullptr);
        if (ed) std::cout << "  editor " << ed->getWidth() << "x" << ed->getHeight() << " opened in " << juce::String (editorMs, 1) << " ms\n";
        if (ed) { ed->setSize (1800, 1200); CHECK (ed->getWidth() == 1800); }
        ed.reset();
        inst.reset();
    });
    // 300 ms budget on a real desktop. Shared CI runners (software GL, cold font
    // caches) are far slower, so CI may raise the limit via MBX_EDITOR_MS_LIMIT.
    double limit = 300.0;
    if (auto* env = std::getenv ("MBX_EDITOR_MS_LIMIT")) limit = std::atof (env);
    std::cout << "  editor-open budget " << limit << " ms\n";
    CHECK (editorMs < limit);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: mbx_host_check <plugin.vst3>\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String path (argv[1]);
    std::thread worker ([&] { run (path); juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();
    std::cout << (failures == 0 ? "HOST CHECK PASSED\n" : "HOST CHECK FAILED\n");
    return failures == 0 ? 0 : 1;
}
