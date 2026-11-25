// Mystery Box Synthesizer by Raygun
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "util/RealtimeGuard.h"

namespace mbx
{
//==============================================================================
const PresetLibrary& LibraryHolder::get()
{
    std::call_once (once, [this]
    {
        std::string err;
        if (! lib.loadFromMemory (BinaryData::presets_bin, (size_t) BinaryData::presets_binSize, &err))
            DBG ("Mystery Box: preset library failed to load: " << err);
    });
    return lib;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MysteryBoxAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; };
    auto secs = [] (float v, int) { return v < 1.0f ? String (juce::roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; };

    auto macros = std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", "|");
    for (int i = 0; i < 8; ++i)
        macros->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::macro[i], 1 }, ids::macroNames[i],
                          NormalisableRange<float> (0.0f, 1.0f), 0.5f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    macros->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::overclock, 1 }, "Overclocked",
                      NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    layout.add (std::move (macros));

    auto voice = std::make_unique<AudioProcessorParameterGroup> ("voice", "Voice", "|");
    auto timeRange = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.03f); return r; };
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::ampA, 1 }, "Attack", timeRange (0.0005f, 20.0f), 0.003f, AudioParameterFloatAttributes().withStringFromValueFunction (secs)));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::ampD, 1 }, "Decay", timeRange (0.001f, 20.0f), 0.3f, AudioParameterFloatAttributes().withStringFromValueFunction (secs)));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::ampS, 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.8f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::ampR, 1 }, "Release", timeRange (0.001f, 30.0f), 0.35f, AudioParameterFloatAttributes().withStringFromValueFunction (secs)));
    NormalisableRange<float> cut (20.0f, 20000.0f); cut.setSkewForCentre (1000.0f);
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::cutoff, 1 }, "Cutoff", cut, 8000.0f,
                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; })));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::resonance, 1 }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.15f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    NormalisableRange<float> gl (0.0f, 2.0f); gl.setSkewForCentre (0.2f);
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::glide, 1 }, "Glide", gl, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (secs)));
    voice->addChild (std::make_unique<AudioParameterInt> (ParameterID { ids::voices, 1 }, "Voices", 1, 32, 16));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::volume, 1 }, "Volume", NormalisableRange<float> (-60.0f, 6.0f, 0.0f, 2.5f), 0.0f,
                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf dB") : String (v, 1) + " dB"; })));
    voice->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::pan, 1 }, "Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.01f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v) * 100.0f)); })));
    layout.add (std::move (voice));

    auto fxGroup = std::make_unique<AudioProcessorParameterGroup> ("fx", "FX", "|");
    for (int i = 0; i < kNumFx; ++i)
    {
        const auto& d = ids::fx[(size_t) i];
        fxGroup->addChild (std::make_unique<AudioParameterBool> (ParameterID { ids::fxOn (i), 1 }, String (d.shortName) + " On", d.defaultOn));
        fxGroup->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::fxMix (i), 1 }, String (d.shortName) + " Mix",
                           NormalisableRange<float> (0.0f, 1.0f), d.defaultMix, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        for (int p = 0; p < d.numParams; ++p)
        {
            const auto& pd = d.params[(size_t) p];
            const auto pid = ParameterID { ids::fxParam (i, p), 1 };
            const String name = String (d.shortName) + " " + pd.name;
            if (pd.kind == ids::FxParamKind::Choice)
                fxGroup->addChild (std::make_unique<AudioParameterChoice> (pid, name, StringArray::fromTokens (pd.choices, "|", ""), (int) pd.defaultValue));
            else if (pd.kind == ids::FxParamKind::Bool)
                fxGroup->addChild (std::make_unique<AudioParameterBool> (pid, name, pd.defaultValue > 0.5f));
            else
                fxGroup->addChild (std::make_unique<AudioParameterFloat> (pid, name, NormalisableRange<float> (0.0f, 1.0f), pd.defaultValue,
                                   AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        }
    }
    fxGroup->addChild (std::make_unique<AudioParameterBool> (ParameterID { ids::ambienceOn, 1 }, "Round Ambience", false));
    fxGroup->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::ambienceLevel, 1 }, "Ambience Level", NormalisableRange<float> (0.0f, 1.0f), 0.35f,
                       AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    layout.add (std::move (fxGroup));
    return layout;
}

//==============================================================================
MysteryBoxAudioProcessor::MysteryBoxAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      game (*this)
{
    for (int i = 0; i < 8; ++i) macroParams[i] = apvts.getRawParameterValue (ids::macro[i]);
    overclockParam = apvts.getRawParameterValue (ids::overclock);
    ampParams[0] = apvts.getRawParameterValue (ids::ampA);
    ampParams[1] = apvts.getRawParameterValue (ids::ampD);
    ampParams[2] = apvts.getRawParameterValue (ids::ampS);
    ampParams[3] = apvts.getRawParameterValue (ids::ampR);
    cutoffParam = apvts.getRawParameterValue (ids::cutoff);
    resParam = apvts.getRawParameterValue (ids::resonance);
    glideParam = apvts.getRawParameterValue (ids::glide);
    voicesParam = apvts.getRawParameterValue (ids::voices);
    volumeParam = apvts.getRawParameterValue (ids::volume);
    panParam = apvts.getRawParameterValue (ids::pan);
    ambOnParam = apvts.getRawParameterValue (ids::ambienceOn);
    ambLevelParam = apvts.getRawParameterValue (ids::ambienceLevel);
    for (int i = 0; i < kNumFx; ++i)
    {
        fxOnParams[i] = apvts.getRawParameterValue (ids::fxOn (i));
        fxMixParams[i] = apvts.getRawParameterValue (ids::fxMix (i));
        for (int p = 0; p < ids::fx[(size_t) i].numParams; ++p) fxPParams[i][p] = apvts.getRawParameterValue (ids::fxParam (i, p));
    }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) learnableParams.add (rp);
    for (auto& c : ccMap) c.store (-1);

    applySettings (store->getSettings());
    store->addChangeListener (this);
}

MysteryBoxAudioProcessor::~MysteryBoxAudioProcessor()
{
    store->removeChangeListener (this);
}

bool MysteryBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MysteryBoxAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    maxBlock = juce::jmax (samplesPerBlock, 16);
    scratchL.allocate ((size_t) maxBlock, true); scratchR.allocate ((size_t) maxBlock, true);
    sfxL.allocate ((size_t) maxBlock, true); sfxR.allocate ((size_t) maxBlock, true);
    engine.prepare (sr, maxBlock);
    previewEngine.prepare (sr, maxBlock);
    sfx.prepare (sr);
    fxChain.prepare (sr, maxBlock);
    volumeSmoother.reset (sr, 0.03); volumeSmoother.setImmediate (juce::Decibels::decibelsToGain (volumeParam->load(), -60.0f));
    panSmoother.reset (sr, 0.03); panSmoother.setImmediate (panParam->load());
    keyboardState.reset();
    previewSamplePos = -1;

    const auto& lib = getLibrary();
    if (const int idx = lib.indexOfId (currentId.load()); idx >= 0)
        engine.setParams (lib.at (idx).params, false);
    setLatencySamples (0);
}

void MysteryBoxAudioProcessor::releaseResources()
{
    engine.allNotesOff (true);
    previewEngine.allNotesOff (true);
    sfx.stopAll();
}

void MysteryBoxAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // Not used (double precision is not advertised), but keep the host happy.
    juce::AudioBuffer<float> tmp (buffer.getNumChannels(), buffer.getNumSamples());
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i) tmp.setSample (c, i, (float) buffer.getSample (c, i));
    processBlock (tmp, midi);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i) buffer.setSample (c, i, (double) tmp.getSample (c, i));
}

void MysteryBoxAudioProcessor::handleCommands() noexcept
{
    AudioCommand cmd;
    const auto& lib = library->get(); // already loaded (commands are only issued after the library is available)
    while (commands.pop (cmd))
    {
        switch (cmd.type)
        {
            case AudioCommand::LoadPreset:
                if (cmd.presetIndex >= 0 && cmd.presetIndex < lib.size())
                    engine.setParams (lib.at (cmd.presetIndex).params, true); // 5 ms out + 5 ms in, held notes re-triggered
                break;
            case AudioCommand::Preview:
                if (cmd.presetIndex >= 0 && cmd.presetIndex < lib.size())
                {
                    previewEngine.allNotesOff (true);
                    previewEngine.setParams (lib.at (cmd.presetIndex).params, false);
                    float m[8];
                    for (int k = 0; k < 8; ++k) m[k] = lib.at (cmd.presetIndex).params.v[(size_t) ((int) P::mac1Val + k * kMacroStride)];
                    previewEngine.setMacroValues (m);
                    for (int k = 0; k < 4; ++k) previewNotes[k] = cmd.previewNotes[k];
                    previewSamplePos = 0;
                    previewHeld = -1;
                }
                break;
            case AudioCommand::Sfx: sfx.trigger (cmd.sfx); break;
            case AudioCommand::Panic:
                engine.allNotesOff (true); previewEngine.allNotesOff (true); sfx.stopAll(); previewSamplePos = -1;
                break;
        }
    }
}

void MysteryBoxAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    {
        const int ch = m.getChannel() - 1;
        if (m.isNoteOn())
        {
            engine.noteOn (ch, m.getNoteNumber(), m.getFloatVelocity());
            auto& e = pitchClassEnergy[m.getNoteNumber() % 12];
            e.store (e.load (std::memory_order_relaxed) + 0.5f + m.getFloatVelocity(), std::memory_order_relaxed);
            midiShots.push (m.getFloatVelocity());
        }
        else if (m.isNoteOff()) engine.noteOff (ch, m.getNoteNumber());
        else if (m.isPitchWheel()) engine.pitchBend (ch, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
        else if (m.isChannelPressure()) engine.channelPressure (ch, (float) m.getChannelPressureValue() / 127.0f);
        else if (m.isAftertouch()) engine.polyPressure (ch, m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const float v = (float) m.getControllerValue() / 127.0f;
            const int target = learnTarget.load (std::memory_order_relaxed);
            if (target >= 0 && cc != 64 && cc != 120 && cc != 123)
            {
                for (auto& slot : ccMap) if (slot.load() == target) slot.store (-1);
                ccMap[cc].store (target);
                learnTarget.store (-1);
            }
            const int mapped = ccMap[cc].load (std::memory_order_relaxed);
            if (mapped >= 0 && mapped < learnableParams.size())
                learnableParams.getUnchecked (mapped)->setValueNotifyingHost (v);
            else
                engine.controller (ch, cc, v);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff (m.isAllSoundOff());
    }
}

void MysteryBoxAudioProcessor::updateFxParams() noexcept
{
    for (int i = 0; i < kNumFx; ++i)
    {
        auto& u = fxParams.unit[(size_t) i];
        const auto& d = ids::fx[(size_t) i];
        u.on = fxOnParams[i]->load() >= 0.5f;
        u.mix = fxMixParams[i]->load();
        for (int p = 0; p < d.numParams; ++p) u.p[p] = fxPParams[i][p]->load();
    }
    // Choice params arrive as indices: convert to the normalised 0..1 the FX chain expects.
    fxParams.unit[(int) FxId::Drive].p[0] /= 2.0f;
    fxParams.unit[(int) FxId::Delay].p[0] /= 11.0f;
    fxParams.unit[(int) FxId::Reverb].p[0] /= 4.0f;

    const auto packed = fxOrderPacked.load (std::memory_order_relaxed);
    for (int s = 0; s < kNumFx; ++s) fxParams.order[(size_t) s] = (int) ((packed >> (4 * s)) & 0xF);
    fxParams.bpm = hostBpm.load (std::memory_order_relaxed);
    fxParams.oversample = oversampling.load (std::memory_order_relaxed);
    fxParams.ambienceOn = ambOnParam->load() >= 0.5f;
    fxParams.ambienceLevel = ambLevelParam->load();
    fxParams.overclock = overclocked.load (std::memory_order_relaxed) ? overclockParam->load() : 0.0f;
    fxParams.reverbMixOffset = engine.getGlobalOffset (ModDest::ReverbMix);
    fxParams.delayMixOffset = engine.getGlobalOffset (ModDest::DelayMix);
    fxParams.chorusMixOffset = engine.getGlobalOffset (ModDest::ChorusMix);
}

void MysteryBoxAudioProcessor::renderPreview (float* L, float* R, int n) noexcept
{
    if (previewSamplePos < 0) return;
    // Phrase: four notes, 0.22 s apart, the last one held; ~2.4 s total.
    const int step = (int) (0.22 * sampleRate);
    int pos = 0;
    while (pos < n)
    {
        const int seg = juce::jmin (n - pos, 64);
        const int t = previewSamplePos;
        const int noteIdx = t / step;
        if (noteIdx < 4 && noteIdx != previewHeld)
        {
            if (previewHeld >= 0 && previewHeld < 3) previewEngine.noteOff (0, previewNotes[previewHeld]);
            previewEngine.noteOn (0, previewNotes[noteIdx], 0.85f);
            previewHeld = noteIdx;
        }
        if (t >= (int) (1.6 * sampleRate) && previewHeld == 3) { previewEngine.noteOff (0, previewNotes[3]); previewHeld = 4; }
        previewEngine.render (scratchL.get(), scratchR.get(), seg);
        juce::FloatVectorOperations::add (L + pos, scratchL.get(), seg);
        juce::FloatVectorOperations::add (R + pos, scratchR.get(), seg);
        previewSamplePos += seg;
        pos += seg;
        if (previewSamplePos > (int) (4.0 * sampleRate)) { previewSamplePos = -1; previewEngine.allNotesOff (true); break; }
    }
}

void MysteryBoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    MBX_REALTIME_SCOPE;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh <= 0) return;
    for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, n);

    // Host transport
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm.store (juce::jlimit (20.0, 999.0, *bpm), std::memory_order_relaxed);
            hostPlaying.store (pos->getIsPlaying(), std::memory_order_relaxed);
        }

    handleCommands();

    // Engine globals + host-parameter overrides
    EngineGlobals g;
    g.a4 = a4.load (std::memory_order_relaxed);
    g.bendRange = (float) bendRange.load (std::memory_order_relaxed);
    g.mpe = mpe.load (std::memory_order_relaxed);
    g.maxPolyphony = juce::jmin ((int) voicesParam->load(), polyLimit.load (std::memory_order_relaxed));
    g.stealMode = stealMode.load (std::memory_order_relaxed);
    g.oversample = oversampling.load (std::memory_order_relaxed);
    g.overclock = overclocked.load (std::memory_order_relaxed) ? overclockParam->load() : 0.0f;
    g.bpm = hostBpm.load (std::memory_order_relaxed);
    engine.setGlobals (g);
    auto pg = g; pg.overclock = 0.0f; pg.maxPolyphony = 8;
    previewEngine.setGlobals (pg);
    {
        auto& p = engine.editParams();
        p[P::ampA] = ampParams[0]->load(); p[P::ampD] = ampParams[1]->load();
        p[P::ampS] = ampParams[2]->load(); p[P::ampR] = ampParams[3]->load();
        p[P::f1Cutoff] = cutoffParam->load(); p[P::f1Res] = resParam->load();
        p[P::glide] = glideParam->load();
    }
    float macros[8];
    for (int i = 0; i < 8; ++i) macros[i] = macroParams[i]->load();
    engine.setMacroValues (macros);
    updateFxParams();

    // On-screen keyboard: merge clicks into the MIDI stream and show incoming notes
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // Process in sub-blocks no larger than the prepared size; split further at MIDI events
    // so note timing is sample accurate. Nothing in here allocates.
    float* outL = buffer.getWritePointer (0);
    float* outR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    auto it = midi.cbegin();
    int pos = 0;
    while (pos < n)
    {
        const int chunk = juce::jmin (n - pos, maxBlock);
        float* L = outL + pos;
        float* R = outR != nullptr ? outR + pos : scratchR.get();
        int sub = 0;
        while (sub < chunk)
        {
            int end = chunk;
            while (it != midi.cend())
            {
                const auto meta = *it;
                const int at = meta.samplePosition - pos;
                if (at > sub) { end = juce::jmin (end, at); break; }
                handleMidi (meta.getMessage());
                ++it;
            }
            engine.render (L + sub, R + sub, end - sub);
            sub = end;
        }
        processChunk (L, R, chunk);
        if (outR == nullptr)
            for (int i = 0; i < chunk; ++i) L[i] = 0.5f * (L[i] + R[i]);
        pos += chunk;
    }
    while (it != midi.cend()) { handleMidi ((*it).getMessage()); ++it; }

    // Meters
    float peak = 0.0f;
    for (int c = 0; c < juce::jmin (2, numCh); ++c) peak = juce::jmax (peak, buffer.getMagnitude (c, 0, n));
    outputPeak.store (juce::jmax (peak, outputPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    activeVoices.store (engine.getActiveVoiceCount(), std::memory_order_relaxed);
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / ((double) n / sampleRate));
    cpuLoad.store (cpuLoad.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

void MysteryBoxAudioProcessor::processChunk (float* L, float* R, int n) noexcept
{
    // Spin preview: in the standalone app always; in a DAW only while the transport is stopped
    if (previewEnabled.load (std::memory_order_relaxed) && (isStandalone() || ! hostPlaying.load (std::memory_order_relaxed)))
        renderPreview (L, R, n);
    else if (previewSamplePos >= 0) { previewSamplePos = -1; previewEngine.allNotesOff (true); }

    // Volume + pan (smoothed)
    volumeSmoother.setTarget (volumeParam->load() <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (volumeParam->load()));
    panSmoother.setTarget (panParam->load());
    for (int i = 0; i < n; ++i)
    {
        const float v = volumeSmoother.next();
        const float pn = panSmoother.next();
        const float gl = pn > 0.0f ? 1.0f - pn : 1.0f, gr = pn < 0.0f ? 1.0f + pn : 1.0f;
        L[i] *= v * gl; R[i] *= v * gr;
    }

    // FX chain (the always-on -0.3 dBFS safety limiter runs inside, last)
    fxChain.process (L, R, n, fxParams);

    // Game SFX: never in the mix unless standalone or explicitly enabled in Settings
    if (sfx.activeCount() > 0)
    {
        juce::FloatVectorOperations::clear (sfxL.get(), n);
        juce::FloatVectorOperations::clear (sfxR.get(), n);
        const bool toOutput = isStandalone() || sfxToOutput.load (std::memory_order_relaxed);
        sfx.render (sfxL.get(), sfxR.get(), n, toOutput ? sfxVolume.load (std::memory_order_relaxed) : 0.0f);
        if (toOutput)
        {
            constexpr float ceiling = 0.96605f;
            for (int i = 0; i < n; ++i)
            {
                L[i] = juce::jlimit (-ceiling, ceiling, L[i] + fastTanh (sfxL[i]) * 0.9f);
                R[i] = juce::jlimit (-ceiling, ceiling, R[i] + fastTanh (sfxR[i]) * 0.9f);
            }
        }
    }
}

//==============================================================================
void MysteryBoxAudioProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

void MysteryBoxAudioProcessor::loadInstrument (int id, bool applyToParams, bool overclockedState)
{
    const auto& lib = getLibrary();
    const int idx = lib.indexOfId (id);
    if (idx < 0) return;
    const auto& info = lib.at (idx);
    if (applyToParams)
    {
        // Host parameters first, so the audio thread sees the new values when it swaps presets.
        for (int k = 0; k < 8; ++k) setParam (ids::macro[k], info.params.v[(size_t) ((int) P::mac1Val + k * kMacroStride)]);
        setParam (ids::ampA, info.params[P::ampA]);
        setParam (ids::ampD, info.params[P::ampD]);
        setParam (ids::ampS, info.params[P::ampS]);
        setParam (ids::ampR, info.params[P::ampR]);
        setParam (ids::cutoff, info.params[P::f1Cutoff]);
        setParam (ids::resonance, info.params[P::f1Res]);
        setParam (ids::glide, info.params[P::glide]);
        if (fxFollowPreset.load())
        {
            const int rev = (int) FxId::Reverb, dly = (int) FxId::Delay, cho = (int) FxId::Chorus;
            setParam (ids::fxOn (rev), info.params[P::fxRevMix] > 0.01f ? 1.0f : 0.0f);
            setParam (ids::fxMix (rev), juce::jmax (0.0f, info.params[P::fxRevMix]));
            setParam (ids::fxParam (rev, 0), std::round (info.params[P::fxRevMode]));
            setParam (ids::fxParam (rev, 1), info.params[P::fxRevSize]);
            setParam (ids::fxOn (dly), info.params[P::fxDlyMix] > 0.01f ? 1.0f : 0.0f);
            if (info.params[P::fxDlyMix] > 0.01f) setParam (ids::fxMix (dly), info.params[P::fxDlyMix]);
            setParam (ids::fxParam (dly, 0), std::round (info.params[P::fxDlyDiv]));
            setParam (ids::fxOn (cho), info.params[P::fxChoMix] > 0.01f ? 1.0f : 0.0f);
            if (info.params[P::fxChoMix] > 0.01f) setParam (ids::fxMix (cho), info.params[P::fxChoMix]);
        }
    }
    currentId.store (id);
    overclocked.store (overclockedState);
    AudioCommand cmd;
    cmd.type = AudioCommand::LoadPreset;
    cmd.presetIndex = idx;
    commands.push (cmd);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::String MysteryBoxAudioProcessor::getCurrentInstrumentName()
{
    const auto* p = getLibrary().findById (currentId.load());
    if (p == nullptr) return "Empty Box";
    return juce::String (juce::CharPointer_UTF8 (p->name.c_str())) + (overclocked.load() ? juce::String (juce::CharPointer_UTF8 (" \xe2\x9a\xa1")) : juce::String());
}

void MysteryBoxAudioProcessor::setOverclocked (bool b)
{
    overclocked.store (b);
    if (b) setParam (ids::overclock, 0.6f);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void MysteryBoxAudioProcessor::startPreview (int id)
{
    const int idx = getLibrary().indexOfId (id);
    if (idx < 0) return;
    AudioCommand cmd;
    cmd.type = AudioCommand::Preview;
    cmd.presetIndex = idx;
    const auto& info = getLibrary().at (idx);
    // Bass categories preview an octave down, leads/bells an octave up
    const int base = (info.category >= 10 && info.category <= 12) ? -24 : (info.category == 3 ? 0 : -12);
    for (int k = 0; k < 4; ++k) cmd.previewNotes[k] = juce::jlimit (24, 108, chimeNote (k == 3 ? 7 : k * 2) + base);
    commands.push (cmd);
}

void MysteryBoxAudioProcessor::playSfx (const SfxEvent& e)
{
    AudioCommand cmd;
    cmd.type = AudioCommand::Sfx;
    cmd.sfx = e;
    commands.push (cmd);
}

void MysteryBoxAudioProcessor::panic()
{
    AudioCommand cmd;
    cmd.type = AudioCommand::Panic;
    commands.push (cmd);
    keyboardState.allNotesOff (0);
}

void MysteryBoxAudioProcessor::addPoints (int delta)
{
    points.store (juce::jmax (0, points.load() + delta));
    if (delta > 0) store->addLifetimePoints (delta);
}

void MysteryBoxAudioProcessor::pushInventory (int id, bool taken)
{
    const juce::ScopedLock sl (stateLock);
    inventory.insert (0, { id, taken, false });
    while (inventory.size() > 10) inventory.removeLast();
}

void MysteryBoxAudioProcessor::markInventoryTaken (int id)
{
    const juce::ScopedLock sl (stateLock);
    for (auto& it : inventory) if (it.id == id) { it.taken = true; break; }
}

void MysteryBoxAudioProcessor::getChimeKey (int& root, bool& minor)
{
    root = 0; minor = true; // C minor by default
    if (! chimeFollowsKey.load()) return;
    float e[12]; float total = 0.0f;
    for (int i = 0; i < 12; ++i) { e[i] = pitchClassEnergy[i].load(); total += e[i]; }
    if (total < 6.0f) return; // not enough notes played yet
    // Krumhansl-Schmuckler key finding
    static constexpr float major[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static constexpr float minorP[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float best = -1.0e9f;
    for (int r = 0; r < 12; ++r)
        for (int m = 0; m < 2; ++m)
        {
            float s = 0.0f;
            for (int i = 0; i < 12; ++i) s += e[(i + r) % 12] * (m ? minorP[i] : major[i]);
            if (s > best) { best = s; root = r; minor = m == 1; }
        }
}

int MysteryBoxAudioProcessor::chimeNote (int degree)
{
    int root; bool minor;
    getChimeKey (root, minor);
    static constexpr int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int d = ((degree % 14) + 14) % 14;
    const int oct = d / 7;
    return 72 + root + 12 * oct + (minor ? mnr[d % 7] : maj[d % 7]);
}

int MysteryBoxAudioProcessor::popMidiShots (float* velocities, int max)
{
    int n = 0; float v;
    while (n < max && midiShots.pop (v)) velocities[n++] = v;
    // Decay the key histogram a little every time the game polls (~60 Hz)
    for (auto& e : pitchClassEnergy) e.store (e.load() * 0.9995f);
    return n;
}

void MysteryBoxAudioProcessor::applySettings (const UserSettings& s)
{
    sfxVolume = s.sfxVolume;
    sfxToOutput = s.sfxToOutput;
    previewEnabled = s.spinPreview;
    oversampling = s.oversampling;
    mpe = s.mpe;
    polyLimit = s.polyphonyLimit;
    stealMode = s.stealMode;
    bendRange = s.bendRange;
    a4 = s.a4;
    chimeFollowsKey = s.chimeFollowsKey;
    fxFollowPreset = s.fxFollowPreset;
}

void MysteryBoxAudioProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applySettings (store->getSettings());
}

void MysteryBoxAudioProcessor::beginMidiLearn (const juce::String& paramId)
{
    for (int i = 0; i < learnableParams.size(); ++i)
        if (learnableParams[i]->getParameterID() == paramId) { learnTarget.store (i); return; }
}

void MysteryBoxAudioProcessor::clearMidiMapping (const juce::String& paramId)
{
    for (int i = 0; i < learnableParams.size(); ++i)
        if (learnableParams[i]->getParameterID() == paramId)
            for (auto& c : ccMap) if (c.load() == i) c.store (-1);
}

int MysteryBoxAudioProcessor::getMidiMapping (const juce::String& paramId) const
{
    for (int i = 0; i < learnableParams.size(); ++i)
        if (learnableParams[i]->getParameterID() == paramId)
            for (int cc = 0; cc < 128; ++cc) if (ccMap[cc].load() == i) return cc;
    return -1;
}

std::array<int, kNumFx> MysteryBoxAudioProcessor::getFxOrder() const
{
    std::array<int, kNumFx> o {};
    const auto packed = fxOrderPacked.load();
    for (int s = 0; s < kNumFx; ++s) o[(size_t) s] = (int) ((packed >> (4 * s)) & 0xF);
    return o;
}

void MysteryBoxAudioProcessor::setFxOrder (const std::array<int, kNumFx>& order)
{
    // Validate: must be a permutation of 0..8
    std::array<bool, kNumFx> seen {};
    for (int v : order) { if (v < 0 || v >= kNumFx || seen[(size_t) v]) return; seen[(size_t) v] = true; }
    uint64_t packed = 0;
    for (int s = 0; s < kNumFx; ++s) packed |= (uint64_t) order[(size_t) s] << (4 * s);
    fxOrderPacked.store (packed);
}

//==============================================================================
// GameHost
void MysteryBoxAudioProcessor::gameAddPoints (int pts) { addPoints (pts); }

bool MysteryBoxAudioProcessor::gameSpendPoints (int pts)
{
    if (points.load() < pts) return false;
    points.store (points.load() - pts);
    return true;
}

void MysteryBoxAudioProcessor::gameSfx (Sfx type, float note, float gain, float pan)
{
    playSfx ({ type, note, gain, pan });
}

MysteryBoxRoller::Result MysteryBoxAudioProcessor::gameRollBox()
{
    store->addLifetimeSpin();
    return roller.roll (getLibrary(), store->getSettings().mascotEnabled);
}

void MysteryBoxAudioProcessor::gameBoxLanded (int presetId)
{
    store->markDiscovered (presetId);
    if (store->getSettings().spinPreview) startPreview (presetId);
}

void MysteryBoxAudioProcessor::gameTakeInstrument (int presetId)
{
    pushInventory (presetId, true);
    loadInstrument (presetId, true, false);
}

void MysteryBoxAudioProcessor::gameLostInstrument (int presetId)
{
    pushInventory (presetId, false);
}

int MysteryBoxAudioProcessor::gameDisplayPreset (uint32_t seed)
{
    const auto& lib = getLibrary();
    if (lib.size() == 0) return 0;
    return lib.at ((int) (seed % (uint32_t) lib.size())).id;
}

const char* MysteryBoxAudioProcessor::gamePresetName (int presetId)
{
    const auto* p = getLibrary().findById (presetId);
    return p ? p->name.c_str() : "";
}

int MysteryBoxAudioProcessor::gamePresetCategory (int presetId)
{
    const auto* p = getLibrary().findById (presetId);
    return p ? p->category : 0;
}

int MysteryBoxAudioProcessor::gamePresetRarity (int presetId)
{
    const auto* p = getLibrary().findById (presetId);
    return p ? p->rarity : 0;
}

void MysteryBoxAudioProcessor::gameKonami()
{
    if (store->getFlag ("konami")) return; // one-time bonus
    store->setFlag ("konami", true);
    addPoints (10000);
    playSfx ({ Sfx::KonamiFanfare, 72, 0.9f, 0 });
    game.konamiMessage = "YOU FOUND IT.";
    game.konamiTimer = 4.0f;
}

//==============================================================================
void MysteryBoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree mbxState ("MBX");
    mbxState.setProperty ("version", JucePlugin_VersionString, nullptr);
    mbxState.setProperty ("instrumentId", currentId.load(), nullptr);
    mbxState.setProperty ("overclocked", overclocked.load(), nullptr);
    mbxState.setProperty ("points", points.load(), nullptr);
    mbxState.setProperty ("round", game.round, nullptr);
    mbxState.setProperty ("rng", juce::String (roller.serialise()), nullptr);
    mbxState.setProperty ("boxX", game.boxX, nullptr);
    mbxState.setProperty ("boxY", game.boxY, nullptr);
    juce::StringArray order;
    for (int v : getFxOrder()) order.add (juce::String (v));
    mbxState.setProperty ("fxOrder", order.joinIntoString (","), nullptr);
    juce::StringArray inv;
    {
        const juce::ScopedLock sl (stateLock);
        for (auto& it : inventory) inv.add (juce::String (it.id) + (it.taken ? "t" : "l"));
    }
    mbxState.setProperty ("inventory", inv.joinIntoString (","), nullptr);
    juce::StringArray cc;
    for (int i = 0; i < 128; ++i)
        if (const int t = ccMap[i].load(); t >= 0 && t < learnableParams.size())
            cc.add (juce::String (i) + "=" + learnableParams[t]->getParameterID());
    mbxState.setProperty ("midiMap", cc.joinIntoString (","), nullptr);
    // A pending (un-taken) box result is resolved as lost when saving: no free re-rolls.
    if (game.boxState == GameWorld::BoxState::Landed && ! game.resultIsMascot)
        mbxState.setProperty ("pendingLost", game.resultPreset, nullptr);

    auto existing = state.getChildWithName ("MBX");
    if (existing.isValid()) state.removeChild (existing, nullptr);
    state.addChild (mbxState, -1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void MysteryBoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto mbxState = state.getChildWithName ("MBX");
    if (mbxState.isValid()) state.removeChild (mbxState, nullptr);
    apvts.replaceState (state);
    if (! mbxState.isValid()) return;

    points.store (juce::jmax (0, (int) mbxState.getProperty ("points", 0)));
    game.round = juce::jmax (1, (int) mbxState.getProperty ("round", 1));
    game.boxX = juce::jlimit (60.0f, GameWorld::kWidth - 60.0f, (float) (double) mbxState.getProperty ("boxX", 360.0));
    game.boxY = juce::jlimit (50.0f, 160.0f, (float) (double) mbxState.getProperty ("boxY", 74.0));
    roller.deserialise (mbxState.getProperty ("rng", "").toString().toStdString());

    std::array<int, kNumFx> order {};
    auto tokens = juce::StringArray::fromTokens (mbxState.getProperty ("fxOrder", "").toString(), ",", "");
    if (tokens.size() == kNumFx) { for (int i = 0; i < kNumFx; ++i) order[(size_t) i] = tokens[i].getIntValue(); setFxOrder (order); }

    {
        const juce::ScopedLock sl (stateLock);
        inventory.clear();
        for (auto& t : juce::StringArray::fromTokens (mbxState.getProperty ("inventory", "").toString(), ",", ""))
            if (t.isNotEmpty()) inventory.add ({ t.getIntValue(), t.endsWithChar ('t'), false });
        if (const int lost = (int) mbxState.getProperty ("pendingLost", 0); lost > 0)
        {
            bool present = false;
            for (auto& it : inventory) present |= it.id == lost;
            if (! present) inventory.insert (0, { lost, false, false });
        }
    }

    for (auto& c : ccMap) c.store (-1);
    for (auto& t : juce::StringArray::fromTokens (mbxState.getProperty ("midiMap", "").toString(), ",", ""))
    {
        const int cc = t.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto pid = t.fromFirstOccurrenceOf ("=", false, false);
        for (int i = 0; i < learnableParams.size(); ++i)
            if (learnableParams[i]->getParameterID() == pid && cc >= 0 && cc < 128) ccMap[cc].store (i);
    }

    const int id = (int) mbxState.getProperty ("instrumentId", 0);
    if (id > 0) loadInstrument (id, false, (bool) mbxState.getProperty ("overclocked", false));
}

juce::AudioProcessorEditor* MysteryBoxAudioProcessor::createEditor()
{
    return new MysteryBoxAudioProcessorEditor (*this);
}

} // namespace mbx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mbx::MysteryBoxAudioProcessor();
}
