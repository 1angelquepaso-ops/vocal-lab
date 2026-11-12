// mw_capture — tiny plugin host for measuring our installed plugins (Waves etc.) offline.
//
// Loads plugins the way a DAW does:
//   * the WaveShell is scanned ONCE and the plugin list is saved (scan)
//   * each plugin is then created straight from that saved list (seconds, no rescan)
//   * the plugin is prepared once and audio is streamed through it with no reset per setting
//   * rendering runs on a worker thread while the main thread keeps the message loop running
//
// Commands
//   mw_capture scan   <plugin file .vst3> <list.xml>
//   mw_capture params <list.xml> "<plugin name>" <out.json>
//   mw_capture render <list.xml> "<plugin name>" <in.wav> <settings.json> <outdir> [block]
//       settings.json = { "defaults": {param: raw}, "settings": [ {"id": n, "set": {param: raw}} , ... ] }
//       writes <outdir>/out_<id>.wav (32-bit float) per setting; prints "R <id> <seconds>" lines
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>
#include <iostream>

using namespace juce;

static void say (const String& s) { std::cout << s << std::endl; }

static std::unique_ptr<AudioPluginFormatManager> makeFormats()
{
    auto fm = std::make_unique<AudioPluginFormatManager>();
    fm->addFormat (new VST3PluginFormat());
    return fm;
}

static int cmdScan (const String& pluginFile, const File& listFile)
{
    VST3PluginFormat vst3;
    OwnedArray<PluginDescription> types;
    const auto t0 = Time::getMillisecondCounterHiRes();
    vst3.findAllTypesForFile (types, pluginFile);
    KnownPluginList kl;
    for (auto* d : types) kl.addType (*d);
    if (auto xml = kl.createXml()) xml->writeTo (listFile);
    say ("scanned " + String (types.size()) + " plugins in "
         + String ((Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " s");
    return types.isEmpty() ? 2 : 0;
}

static std::unique_ptr<AudioPluginInstance> load (const File& listFile, const String& name, double sr, int block)
{
    KnownPluginList kl;
    if (auto xml = parseXML (listFile)) kl.recreateFromXml (*xml);
    PluginDescription found;
    bool ok = false;
    for (auto& d : kl.getTypes())
        if (d.name == name) { found = d; ok = true; break; }
    if (! ok) { say ("ERROR plugin not in list: " + name); return {}; }

    auto fm = makeFormats();
    String err;
    const auto t0 = Time::getMillisecondCounterHiRes();
    auto inst = fm->createPluginInstance (found, sr, block, err);
    if (inst == nullptr) { say ("ERROR could not create: " + err); return {}; }
    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (sr, block);
    inst->prepareToPlay (sr, block);
    say ("loaded " + String ((Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + " s  in="
         + String (inst->getTotalNumInputChannels()) + " out=" + String (inst->getTotalNumOutputChannels())
         + " latency=" + String (inst->getLatencySamples()));
    return inst;
}

static int cmdParams (const File& listFile, const String& name, const File& out)
{
    auto inst = load (listFile, name, 44100.0, 512);
    if (! inst) return 3;
    Array<var> arr;
    for (auto* p : inst->getParameters())
    {
        auto* o = new DynamicObject();
        o->setProperty ("name", p->getName (256));
        o->setProperty ("index", p->getParameterIndex());
        o->setProperty ("default", p->getDefaultValue());
        o->setProperty ("raw", p->getValue());
        o->setProperty ("steps", p->getNumSteps());
        o->setProperty ("boolean", p->isBoolean());
        o->setProperty ("discrete", p->isDiscrete());
        o->setProperty ("meta", p->isMetaParameter());
        o->setProperty ("label", p->getLabel());
        o->setProperty ("text", p->getCurrentValueAsText());
        auto* at = new DynamicObject();
        for (int i = 0; i <= 8; ++i)
            at->setProperty (String (i / 8.0, 3), p->getText ((float) i / 8.0f, 64));
        o->setProperty ("text_at", var (at));
        arr.add (var (o));
    }
    auto* root = new DynamicObject();
    root->setProperty ("plugin", name);
    root->setProperty ("in", inst->getTotalNumInputChannels());
    root->setProperty ("out", inst->getTotalNumOutputChannels());
    root->setProperty ("latency", inst->getLatencySamples());
    root->setProperty ("params", arr);
    out.replaceWithText (JSON::toString (var (root)));
    say ("params " + String (arr.size()));
    return 0;
}

static int cmdRender (const File& listFile, const String& name, const File& inWav, const File& settingsFile,
                      const File& outDir, int block)
{
    AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (afm.createReaderFor (inWav));
    if (! rd) { say ("ERROR cannot read " + inWav.getFullPathName()); return 4; }
    const double sr = rd->sampleRate;
    AudioBuffer<float> input (1, (int) rd->lengthInSamples);
    rd->read (&input, 0, (int) rd->lengthInSamples, 0, true, false);

    auto settings = JSON::parse (settingsFile);
    auto inst = load (listFile, name, sr, block);
    if (! inst) return 3;
    outDir.createDirectory();

    std::map<String, AudioProcessorParameter*> byName;
    for (auto* p : inst->getParameters()) byName[p->getName (256)] = p;
    auto apply = [&] (const var& obj)
    {
        if (auto* d = obj.getDynamicObject())
            for (auto& kv : d->getProperties())
                if (auto it = byName.find (kv.name.toString()); it != byName.end())
                    it->second->setValue ((float) (double) kv.value);
    };

    const int nIn = jmax (1, inst->getMainBusNumInputChannels()), nOut = inst->getMainBusNumOutputChannels();
    const int lat = jmax (0, inst->getLatencySamples());
    const int nch = jmax (1, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
    const int settle = (int) (1.0 * sr);                 // 1 s of silence after each change (knob smoothing)
    std::atomic<bool> done { false };
    int rc = 0;

    std::thread worker ([&]
    {
        AudioBuffer<float> buf (nch, block);
        MidiBuffer midi;
        auto* list = settings["settings"].getArray();
        for (int s = 0; list && s < list->size(); ++s)
        {
            const auto& item = list->getReference (s);
            const int id = (int) item["id"];
            const auto t0 = Time::getMillisecondCounterHiRes();
            apply (settings["defaults"]);
            apply (item["set"]);
            for (int pos = 0; pos < settle; pos += block)    // let the plugin settle on silence
            {
                buf.clear();
                inst->processBlock (buf, midi);
                midi.clear();
            }
            AudioBuffer<float> out (jmax (1, nOut), input.getNumSamples());
            out.clear();
            const int total = input.getNumSamples() + lat;      // run on by the latency, then drop it
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = jmin (block, total - pos);
                buf.clear();
                const int avail = jlimit (0, n, input.getNumSamples() - pos);
                for (int c = 0; c < nIn && avail > 0; ++c)
                    buf.copyFrom (c, 0, input, 0, pos, avail);
                inst->processBlock (buf, midi);
                midi.clear();
                const int dst0 = pos - lat;                       // where this block lands in the output
                const int from = jmax (0, -dst0), to = jmin (n, out.getNumSamples() - dst0);
                for (int c = 0; c < out.getNumChannels() && to > from; ++c)
                    out.copyFrom (c, dst0 + from, buf, jmin (c, nch - 1), from, to - from);
            }
            auto f = outDir.getChildFile ("out_" + String (id) + ".wav");
            f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), sr,
                                                   (unsigned int) out.getNumChannels(), 32, {}, 0));
            if (! w) { say ("ERROR cannot write " + f.getFullPathName()); rc = 5; break; }
            w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            say ("R " + String (id) + " " + String ((Time::getMillisecondCounterHiRes() - t0) / 1000.0, 3));
        }
        done = true;
    });

    while (! done.load())                                   // keep the message loop alive like a DAW
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    worker.join();
    inst->releaseResources();
    return rc;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    StringArray a;
    for (int i = 1; i < argc; ++i) a.add (String::fromUTF8 (argv[i]));
    const auto cwd = File::getCurrentWorkingDirectory();
    auto file = [&] (const String& s) { return cwd.getChildFile (s); };
    int rc = 1;
    if (a[0] == "scan" && a.size() >= 3)
        rc = cmdScan (a[1], file (a[2]));
    else if (a[0] == "params" && a.size() >= 4)
        rc = cmdParams (file (a[1]), a[2], file (a[3]));
    else if (a[0] == "render" && a.size() >= 6)
        rc = cmdRender (file (a[1]), a[2], file (a[3]), file (a[4]), file (a[5]), a.size() > 6 ? a[6].getIntValue() : 512);
    else
        say ("usage: mw_capture scan|params|render ...");
    say ("EXIT " + String (rc));
    return rc;
}
