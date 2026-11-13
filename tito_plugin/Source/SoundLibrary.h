#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>

// Angel's own sound folder -> drum sounds sorted by type (from file + folder names).
// Scanned once in the background and cached, so shuffling is instant.
class SoundLibrary : private juce::Thread
{
public:
    SoundLibrary() : juce::Thread ("tito library scan") {}
    ~SoundLibrary() override { stopThread (4000); }

    static juce::String classify (const juce::File& f)
    {
        const auto name = f.getFileNameWithoutExtension().toLowerCase();
        const auto dir = f.getParentDirectory().getFileName().toLowerCase()
                       + " " + f.getParentDirectory().getParentDirectory().getFileName().toLowerCase();
        if (name.contains ("loop") || dir.contains ("loop") || name.contains ("bpm")) return {};
        auto has = [] (const juce::String& s, std::initializer_list<const char*> words)
        {
            for (auto* w : words)
            {
                const juce::String word (w);
                int i = s.indexOf (word);
                while (i >= 0)
                {
                    const bool startOk = i == 0 || ! juce::CharacterFunctions::isLetter (s[i - 1]);
                    const bool endOk = i + word.length() >= s.length() || ! juce::CharacterFunctions::isLetter (s[i + word.length()]);
                    if (startOk && (endOk || word.length() >= 4)) return true;
                    i = s.indexOf (i + 1, word);
                }
            }
            return false;
        };
        for (const auto& s : { name, dir })
        {
            if (has (s, { "808", "sub" })) return "808";
            if (has (s, { "open hat", "openhat", "open", "ohh", "oh" })) return "openhat";
            if (has (s, { "hihat", "hi-hat", "hi hat", "hat", "hh", "chh", "closed" })) return "hat";
            if (has (s, { "kick", "kik", "bd", "bassdrum", "bass drum" })) return "kick";
            if (has (s, { "snare", "snr", "sd" })) return "snare";
            if (has (s, { "clap", "clp" })) return "clap";
            if (has (s, { "rim", "rimshot", "sidestick", "stick" })) return "rim";
            if (has (s, { "tom", "toms" })) return "tom";
            if (has (s, { "crash", "cymbal", "splash", "china" })) return "crash";
            if (has (s, { "ride", "bell" })) return "ride";
            if (has (s, { "perc", "conga", "bongo", "timbal", "timbale", "shaker", "shake", "tamb", "tambourine",
                           "cowbell", "clave", "block", "guiro", "agogo", "djembe", "tabla", "dhol", "darbuka" })) return "perc";
            if (has (s, { "fx", "riser", "sweep", "impact", "vox", "chant", "noise", "rev" })) return "fx";
        }
        return {};
    }

    void scan (const juce::File& root, std::function<void()> onDone)
    {
        stopThread (4000);
        folder = root;
        done = std::move (onDone);
        if (! loadCache())
            startThread();
        else if (done)
            juce::MessageManager::callAsync (done);
    }

    void rescan() { cacheFile().deleteFile(); scan (folder, done); }

    bool isScanning() const { return isThreadRunning(); }
    int count() const { const juce::ScopedLock sl (lock); int n = 0; for (auto& [k, v] : byType) n += v.size(); return n; }
    int countOf (const juce::String& type) const { const juce::ScopedLock sl (lock); auto it = byType.find (type); return it == byType.end() ? 0 : it->second.size(); }

    // A random sound of this pad type (with sensible fallbacks).
    juce::File pick (const juce::String& type, juce::Random& rng) const
    {
        const juce::ScopedLock sl (lock);
        static const std::map<juce::String, juce::StringArray> fallback {
            { "808", { "808", "kick" } }, { "openhat", { "openhat", "hat" } }, { "hat", { "hat" } },
            { "clap", { "clap", "snare" } }, { "rim", { "rim", "snare" } }, { "ride", { "ride", "crash" } },
            { "fx", { "fx", "perc" } } };
        juce::StringArray order { type };
        if (auto it = fallback.find (type); it != fallback.end()) order = it->second;
        for (auto& t : order)
            if (auto it = byType.find (t); it != byType.end() && ! it->second.isEmpty())
                return juce::File (it->second[rng.nextInt (it->second.size())]);
        return {};
    }

    juce::File folder;

private:
    mutable juce::CriticalSection lock;
    std::map<juce::String, juce::StringArray> byType;
    std::function<void()> done;

    juce::File cacheFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("TITO PUENTE").getChildFile ("library_" + juce::String::toHexString (folder.getFullPathName().hashCode64()) + ".txt");
    }

    bool loadCache()
    {
        const auto c = cacheFile();
        if (! c.existsAsFile()) return false;
        juce::StringArray lines;
        c.readLines (lines);
        const juce::ScopedLock sl (lock);
        byType.clear();
        for (auto& l : lines)
        {
            const int bar = l.indexOfChar ('|');
            if (bar > 0) byType[l.substring (0, bar)].add (l.substring (bar + 1));
        }
        return ! byType.empty();
    }

    void run() override
    {
        std::map<juce::String, juce::StringArray> found;
        for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*.wav;*.aif;*.aiff;*.flac", juce::File::findFiles))
        {
            if (threadShouldExit()) return;
            const auto& f = e.getFile();
            if (e.getFileSize() > 6 * 1024 * 1024) continue;                  // long files are loops / songs
            const auto t = classify (f);
            if (t.isNotEmpty()) found[t].add (f.getFullPathName());
        }
        juce::String out;
        for (auto& [k, v] : found)
            for (auto& p : v) out << k << "|" << p << "\n";
        cacheFile().getParentDirectory().createDirectory();
        cacheFile().replaceWithText (out);
        {
            const juce::ScopedLock sl (lock);
            byType = std::move (found);
        }
        if (done) juce::MessageManager::callAsync (done);
    }
};
