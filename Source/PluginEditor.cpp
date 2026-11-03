#include "PluginEditor.h"
#include "TrainerAssets.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{
    /** Where the Trainer keeps its WebView2 profile and ui-dir.txt: %LOCALAPPDATA%\MusicLearnTrainer, or the absolute
        folder in MUSICLEARN_TRAINER_DATA when that's set (the check's smoke test gives the Standalone a fresh one). */
    juce::File dataFolder()
    {
        const auto fromEnv = juce::SystemStats::getEnvironmentVariable ("MUSICLEARN_TRAINER_DATA", {});
        jassert (fromEnv.isEmpty() || juce::File::isAbsolutePath (fromEnv));

        if (juce::File::isAbsolutePath (fromEnv))
            return juce::File (fromEnv);

       #if JUCE_WINDOWS
        const auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
       #else
        const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        return base.getChildFile ("MusicLearnTrainer");
    }

    juce::File webViewDataFolder() { return dataFolder().getChildFile ("WebView2"); }

    /** The folder named in ui-dir.txt if it holds an index.html, else an empty File (use the embedded UI). */
    juce::File liveUiFolder()
    {
        const auto config = dataFolder().getChildFile ("ui-dir.txt");

        if (! config.existsAsFile())
            return {};

        const auto path = config.loadFileAsString().trim().unquoted();

        if (! juce::File::isAbsolutePath (path))
            return {};

        const juce::File dir (path);
        return dir.getChildFile ("index.html").existsAsFile() ? dir : juce::File();
    }

    juce::String mimeFor (const juce::String& extension)
    {
        if (extension == "html")  return "text/html";
        if (extension == "js")    return "text/javascript";
        if (extension == "css")   return "text/css";
        if (extension == "json")  return "application/json";
        if (extension == "woff2") return "font/woff2";
        if (extension == "woff")  return "font/woff";
        if (extension == "svg")   return "image/svg+xml";
        if (extension == "png")   return "image/png";
        return "application/octet-stream";
    }
}

juce::WebBrowserComponent::Options TrainerEditor::makeOptions (TrainerEditor& self)
{
    using Options = juce::WebBrowserComponent::Options;

    webViewDataFolder().createDirectory();

    return Options{}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (webViewDataFolder())
                                     .withBackgroundColour (juce::Colour (0xff0f1117))
                                     .withStatusBarDisabled()
                                     .withBuiltInErrorPageDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([dir = self.uiDir] (const auto& url) { return getResource (url, dir); })
        .withInitialisationData ("host", "plugin")
        .withInitialisationData ("ui", self.uiDir == juce::File() ? "embedded" : "live")
        .withEventListener ("ui", [&self] (const juce::var& payload) { self.handleUiEvent (payload); });
}

TrainerEditor::TrainerEditor (TrainerProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      uiDir (liveUiFolder()),
      web (makeOptions (*this))
{
    addAndMakeVisible (web);

    setResizable (true, true);
    setResizeLimits (720, 480, 4096, 4096);
    setSize (juce::jlimit (720, 4096, processor.editorWidth.load()),
             juce::jlimit (480, 4096, processor.editorHeight.load()));

    if (uiDir != juce::File())
        uiSeen = uiDir.getChildFile ("index.html").getLastModificationTime();

    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (60);
}

TrainerEditor::~TrainerEditor()
{
    stopTimer();
}

void TrainerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1117));
}

void TrainerEditor::resized()
{
    web.setBounds (getLocalBounds());
    processor.editorWidth  = getWidth();
    processor.editorHeight = getHeight();
}

void TrainerEditor::timerCallback()
{
    checkLiveUi();

    // Transport first, then notes: the page maps note times onto the host's beat grid using the latest snapshot.
    {
        const auto tr = processor.getTransport();
        auto* o = new juce::DynamicObject();
        o->setProperty ("t",       tr.timeMs);
        o->setProperty ("ppq",     tr.ppq);
        o->setProperty ("bpm",     tr.bpm);
        o->setProperty ("num",     tr.numerator);
        o->setProperty ("den",     tr.denominator);
        o->setProperty ("playing", tr.playing);
        o->setProperty ("host",    tr.hasHost);
        web.emitEventIfBrowserIsVisible ("transport", juce::var (o));
    }

    if (processor.popEvents (scratch) == 0)
        return;

    juce::Array<juce::var> events;

    for (const auto& e : scratch)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("n",  e.note);
        o->setProperty ("v",  e.velocity);
        o->setProperty ("ch", e.channel);
        o->setProperty ("on", e.on);
        o->setProperty ("t",  e.timeMs);
        events.add (juce::var (o));
    }

    web.emitEventIfBrowserIsVisible ("midi", juce::var (events));
}

/** Live UI: every half second, reload the page once index.html has changed and then held still for one
    check, so a save in progress is never loaded half-written. Progress survives: same origin, same profile. */
void TrainerEditor::checkLiveUi()
{
    if (uiDir == juce::File() || ++uiCheckTicks < 30)
        return;

    uiCheckTicks = 0;
    const auto modified = uiDir.getChildFile ("index.html").getLastModificationTime();

    if (modified != uiSeen)
    {
        uiSeen = modified;
        uiChanged = true;
    }
    else if (uiChanged)
    {
        uiChanged = false;
        web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "?v=" + juce::String (modified.toMilliseconds()));
    }
}

void TrainerEditor::handleUiEvent (const juce::var& payload)
{
    const auto type = payload.getProperty ("type", "").toString();

    if (type == "resize")
    {
        const int w = (int) payload.getProperty ("w", getWidth());
        const int h = (int) payload.getProperty ("h", getHeight());
        setSize (juce::jlimit (720, 4096, w), juce::jlimit (480, 4096, h));
    }
    else if (type == "maximise")
    {
        setMaximised (true);
    }
    else if (type == "restore")
    {
        setMaximised (false);
    }
    else if (type == "guide")
    {
        std::vector<ScheduledNote> notes;

        if (const auto* arr = payload.getProperty ("notes", juce::var()).getArray())
            for (const auto& v : *arr)
            {
                ScheduledNote n;
                n.note     = (int) v.getProperty ("n", 0);
                n.velocity = (int) v.getProperty ("v", 0);
                n.channel  = (int) v.getProperty ("ch", 2);
                n.on       = (bool) v.getProperty ("on", false);
                n.timeMs   = (double) v.getProperty ("t", 0.0);
                notes.push_back (n);
            }

        processor.scheduleNotes (notes);
    }
    else if (type == "guideOff")
    {
        processor.cancelGuide();
    }
    else if (type == "log")
    {
        DBG ("[trainer-ui] " << payload.getProperty ("msg", "").toString());
    }
}

void TrainerEditor::setMaximised (bool shouldBeMaximised)
{
    if (shouldBeMaximised)
    {
        savedWidth  = getWidth();
        savedHeight = getHeight();
    }

   #if JUCE_WINDOWS
    // Hosts give plugin windows a close button only. Ask the OS to maximise the
    // top-level window that contains us; the host then resizes the editor to fit.
    if (auto* peer = getPeer())
    {
        if (auto root = GetAncestor ((HWND) peer->getNativeHandle(), GA_ROOT))
        {
            ShowWindow (root, shouldBeMaximised ? SW_MAXIMIZE : SW_RESTORE);

            // If the host ignored the window change, resize the editor ourselves instead.
            const auto w = getWidth(), h = getHeight();
            juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<TrainerEditor> (this), shouldBeMaximised, w, h]
            {
                if (safe == nullptr || safe->getWidth() != w || safe->getHeight() != h)
                    return;

                if (shouldBeMaximised)
                    safe->fillDisplay();
                else
                    safe->setSize (safe->savedWidth, safe->savedHeight);
            });
            return;
        }
    }
   #endif

    if (shouldBeMaximised)
        fillDisplay();
    else
        setSize (savedWidth, savedHeight);
}

void TrainerEditor::fillDisplay()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();

    if (auto* display = displays.getDisplayForRect (getScreenBounds()))
    {
        const auto area = display->userArea;
        setSize (juce::jlimit (720, 4096, area.getWidth() - 24),
                 juce::jlimit (480, 4096, area.getHeight() - 110));
    }
}

std::optional<juce::WebBrowserComponent::Resource> TrainerEditor::getResource (const juce::String& urlIn, const juce::File& uiDir)
{
    auto path = urlIn.upToFirstOccurrenceOf ("?", false, false)
                     .trimCharactersAtStart ("/");

    if (path.isEmpty())
        path = "index.html";

    const auto extension = path.fromLastOccurrenceOf (".", false, false).toLowerCase();

    // Live UI: read the file from disk on every request; anything missing there falls back to the embedded copy.
    if (uiDir != juce::File())
    {
        const auto file = uiDir.getChildFile (path);
        juce::MemoryBlock data;

        if (file.isAChildOf (uiDir) && file.existsAsFile() && file.loadFileAsData (data))
        {
            const auto* bytes = static_cast<const std::byte*> (data.getData());
            return juce::WebBrowserComponent::Resource { std::vector<std::byte> (bytes, bytes + data.getSize()), mimeFor (extension) };
        }
    }

    for (int i = 0; i < TrainerAssets::namedResourceListSize; ++i)
    {
        const auto* resourceName = TrainerAssets::namedResourceList[i];

        if (juce::String (TrainerAssets::getNamedResourceOriginalFilename (resourceName)) != path)
            continue;

        int size = 0;
        const auto* data = TrainerAssets::getNamedResource (resourceName, size);

        if (data == nullptr)
            break;

        const auto* bytes = reinterpret_cast<const std::byte*> (data);

        return juce::WebBrowserComponent::Resource { std::vector<std::byte> (bytes, bytes + size),
                                                     mimeFor (extension) };
    }

    return std::nullopt;
}
