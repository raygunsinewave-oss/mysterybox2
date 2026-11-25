// Mystery Box Synthesizer by Raygun
#include "PersistentStore.h"

namespace mbx
{
namespace
{
juce::PropertiesFile::Options storeOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "MysteryBoxSynth";
    o.filenameSuffix = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    o.folderName = ".config/Raygun/MysteryBoxSynth";
   #else
    o.folderName = "Raygun" + juce::File::getSeparatorString() + "MysteryBoxSynth";
   #endif
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1; // we save explicitly
    o.processLock = nullptr;
    return o;
}

juce::InterProcessLock& fileLock()
{
    static juce::InterProcessLock lock ("RaygunMysteryBoxSynthStore");
    return lock;
}

juce::String bitsToHex (const std::bitset<2500>& b)
{
    juce::String s;
    s.preallocateBytes (640);
    for (size_t i = 0; i < 2500; i += 4)
    {
        int nib = 0;
        for (size_t k = 0; k < 4 && i + k < 2500; ++k) nib |= (b[i + k] ? 1 : 0) << k;
        s << juce::String::toHexString (nib);
    }
    return s;
}

std::bitset<2500> hexToBits (const juce::String& s)
{
    std::bitset<2500> b;
    for (int c = 0; c < s.length(); ++c)
    {
        const int nib = juce::CharacterFunctions::getHexDigitValue (s[c]);
        if (nib < 0) continue;
        for (int k = 0; k < 4; ++k)
        {
            const size_t idx = (size_t) c * 4 + (size_t) k;
            if (idx < 2500) b[idx] = ((nib >> k) & 1) != 0;
        }
    }
    return b;
}
} // namespace

PersistentStore::PersistentStore()
{
    {
        juce::InterProcessLock::ScopedLockType sl (fileLock());
        props = std::make_unique<juce::PropertiesFile> (storeOptions());
    }
    load();
    startTimer (2000); // pick up changes written by instances in other host processes
}

PersistentStore::~PersistentStore()
{
    stopTimer();
    if (dirty) save();
}

juce::File PersistentStore::getStorageFile() const { return props->getFile(); }

void PersistentStore::load()
{
    auto& p = *props;
    UserSettings s;
    s.spinCost = p.getIntValue ("spinCost", s.spinCost);
    if (s.spinCost != 250 && s.spinCost != 500 && s.spinCost != 950 && s.spinCost != 1500) s.spinCost = 950;
    s.freeSpin = p.getBoolValue ("freeSpin", s.freeSpin);
    s.difficulty = juce::jlimit (0, 3, p.getIntValue ("difficulty", s.difficulty));
    s.midiShooting = p.getBoolValue ("midiShooting", s.midiShooting);
    s.mascotEnabled = p.getBoolValue ("mascot", s.mascotEnabled);
    s.spinPreview = p.getBoolValue ("spinPreview", s.spinPreview);
    s.chimeFollowsKey = p.getBoolValue ("chimeFollowsKey", s.chimeFollowsKey);
    s.sfxVolume = (float) juce::jlimit (0.0, 1.0, p.getDoubleValue ("sfxVolume", s.sfxVolume));
    s.sfxToOutput = p.getBoolValue ("sfxToOutput", s.sfxToOutput);
    s.graphics = juce::jlimit (0, 2, p.getIntValue ("graphics", s.graphics));
    s.fpsCap = p.getIntValue ("fpsCap", s.fpsCap) == 30 ? 30 : 60;
    s.uiScalePercent = juce::jlimit (75, 200, p.getIntValue ("uiScale", s.uiScalePercent));
    s.gpuRendering = p.getBoolValue ("gpu", s.gpuRendering);
    s.polyphonyLimit = juce::jlimit (1, 32, p.getIntValue ("polyLimit", s.polyphonyLimit));
    s.oversampling = p.getBoolValue ("oversampling", s.oversampling);
    s.stealMode = juce::jlimit (0, 2, p.getIntValue ("stealMode", s.stealMode));
    s.a4 = (float) juce::jlimit (415.0, 466.0, p.getDoubleValue ("a4", s.a4));
    s.bendRange = juce::jlimit (1, 24, p.getIntValue ("bendRange", s.bendRange));
    s.mpe = p.getBoolValue ("mpe", s.mpe);
    s.fxFollowPreset = p.getBoolValue ("fxFollowPreset", s.fxFollowPreset);
    settings = s;

    discovered = hexToBits (p.getValue ("discovered"));
    favorites.clear();
    for (auto& tok : juce::StringArray::fromTokens (p.getValue ("favorites"), ",", ""))
    {
        auto parts = juce::StringArray::fromTokens (tok, ":", "");
        FavoriteEntry e;
        e.id = parts[0].getIntValue();
        e.addedTime = parts.size() > 1 ? parts[1].getLargeIntValue() : 0;
        if (e.id >= 1 && e.id <= kNumInstruments && ! isFavorite (e.id)) favorites.add (e);
    }
    flags = juce::StringArray::fromTokens (p.getValue ("flags"), ",", "");
    flags.removeEmptyStrings();
    lifetimePoints = p.getValue ("lifetimePoints", "0").getLargeIntValue();
    lifetimeSpins = p.getIntValue ("lifetimeSpins", 0);
    lastKnownModification = getStorageFile().getLastModificationTime();
}

void PersistentStore::save()
{
    juce::InterProcessLock::ScopedLockType sl (fileLock());
    // Merge-on-save: other instances may have discovered instruments meanwhile.
    {
        juce::PropertiesFile fresh (storeOptions());
        discovered |= hexToBits (fresh.getValue ("discovered"));
        lifetimeSpins = juce::jmax (lifetimeSpins, fresh.getIntValue ("lifetimeSpins", 0));
        lifetimePoints = juce::jmax (lifetimePoints, fresh.getValue ("lifetimePoints", "0").getLargeIntValue());
    }
    auto& p = *props;
    const auto& s = settings;
    p.setValue ("spinCost", s.spinCost);
    p.setValue ("freeSpin", s.freeSpin);
    p.setValue ("difficulty", s.difficulty);
    p.setValue ("midiShooting", s.midiShooting);
    p.setValue ("mascot", s.mascotEnabled);
    p.setValue ("spinPreview", s.spinPreview);
    p.setValue ("chimeFollowsKey", s.chimeFollowsKey);
    p.setValue ("sfxVolume", (double) s.sfxVolume);
    p.setValue ("sfxToOutput", s.sfxToOutput);
    p.setValue ("graphics", s.graphics);
    p.setValue ("fpsCap", s.fpsCap);
    p.setValue ("uiScale", s.uiScalePercent);
    p.setValue ("gpu", s.gpuRendering);
    p.setValue ("polyLimit", s.polyphonyLimit);
    p.setValue ("oversampling", s.oversampling);
    p.setValue ("stealMode", s.stealMode);
    p.setValue ("a4", (double) s.a4);
    p.setValue ("bendRange", s.bendRange);
    p.setValue ("mpe", s.mpe);
    p.setValue ("fxFollowPreset", s.fxFollowPreset);
    p.setValue ("discovered", bitsToHex (discovered));
    juce::StringArray fav;
    for (auto& f : favorites) fav.add (juce::String (f.id) + ":" + juce::String (f.addedTime));
    p.setValue ("favorites", fav.joinIntoString (","));
    p.setValue ("flags", flags.joinIntoString (","));
    p.setValue ("lifetimePoints", juce::String (lifetimePoints));
    p.setValue ("lifetimeSpins", lifetimeSpins);
    p.saveIfNeeded();
    lastKnownModification = getStorageFile().getLastModificationTime();
    dirty = false;
}

void PersistentStore::timerCallback()
{
    const auto mod = getStorageFile().getLastModificationTime();
    if (mod > lastKnownModification)
    {
        juce::InterProcessLock::ScopedLockType sl (fileLock());
        props->reload();
        load();
        sendChangeMessage();
    }
}

void PersistentStore::setSettings (const UserSettings& s)
{
    settings = s;
    save();
    sendChangeMessage();
}

void PersistentStore::markDiscovered (int id)
{
    if (id < 1 || id > kNumInstruments || discovered[(size_t) (id - 1)]) return;
    discovered[(size_t) (id - 1)] = true;
    save();
    sendChangeMessage();
}

bool PersistentStore::isFavorite (int id) const noexcept
{
    for (auto& f : favorites) if (f.id == id) return true;
    return false;
}

void PersistentStore::setFavorite (int id, bool fav)
{
    if (id < 1 || id > kNumInstruments || isFavorite (id) == fav) return;
    if (fav) favorites.add ({ id, juce::Time::currentTimeMillis() });
    else for (int i = favorites.size(); --i >= 0;) if (favorites[i].id == id) favorites.remove (i);
    save();
    sendChangeMessage();
}

bool PersistentStore::exportFavorites (const juce::File& file) const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("format", "mbxfav");
    root->setProperty ("version", 1);
    juce::Array<juce::var> ids;
    for (auto& f : favorites) ids.add (f.id);
    root->setProperty ("favorites", ids);
    return file.replaceWithText (juce::JSON::toString (juce::var (root.get())));
}

int PersistentStore::importFavorites (const juce::File& file)
{
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (parsed.getProperty ("format", {}).toString() != "mbxfav") return 0;
    int n = 0;
    if (auto* arr = parsed.getProperty ("favorites", {}).getArray())
        for (auto& v : *arr)
        {
            const int id = (int) v;
            // Only instruments you have actually won can be favourited.
            if (id >= 1 && id <= kNumInstruments && isDiscovered (id) && ! isFavorite (id))
            { favorites.add ({ id, juce::Time::currentTimeMillis() }); ++n; }
        }
    if (n > 0) { save(); sendChangeMessage(); }
    return n;
}

bool PersistentStore::getFlag (const juce::String& name) const { return flags.contains (name); }

void PersistentStore::setFlag (const juce::String& name, bool v)
{
    if (flags.contains (name) == v) return;
    if (v) flags.add (name); else flags.removeString (name);
    save();
    sendChangeMessage();
}

void PersistentStore::addLifetimePoints (int pts)
{
    lifetimePoints += pts;
    dirty = true; // saved lazily (frequent); flushed with the next explicit save or on destruction
}

void PersistentStore::addLifetimeSpin()
{
    ++lifetimeSpins;
    save();
}

void PersistentStore::resetProgress()
{
    discovered.reset();
    favorites.clear();
    flags.clear();
    lifetimePoints = 0;
    lifetimeSpins = 0;
    juce::InterProcessLock::ScopedLockType sl (fileLock());
    auto& p = *props;
    p.setValue ("discovered", juce::String());
    p.setValue ("favorites", juce::String());
    p.setValue ("flags", juce::String());
    p.setValue ("lifetimePoints", "0");
    p.setValue ("lifetimeSpins", 0);
    p.saveIfNeeded();
    lastKnownModification = getStorageFile().getLastModificationTime();
    sendChangeMessage();
}

} // namespace mbx
