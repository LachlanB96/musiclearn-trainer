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
    juce::File webViewDataFolder()
    {
       #if JUCE_WINDOWS
        const auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
       #else
        const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        return base.getChildFile ("MusicLearnTrainer").getChildFile ("WebView2");
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
        .withResourceProvider ([] (const auto& url) { return getResource (url); })
        .withInitialisationData ("host", "plugin")
        .withEventListener ("ui", [&self] (const juce::var& payload) { self.handleUiEvent (payload); });
}

TrainerEditor::TrainerEditor (TrainerProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      web (makeOptions (*this))
{
    addAndMakeVisible (web);

    setResizable (true, true);
    setResizeLimits (720, 480, 4096, 4096);
    setSize (juce::jlimit (720, 4096, processor.editorWidth.load()),
             juce::jlimit (480, 4096, processor.editorHeight.load()));

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

std::optional<juce::WebBrowserComponent::Resource> TrainerEditor::getResource (const juce::String& urlIn)
{
    auto path = urlIn.upToFirstOccurrenceOf ("?", false, false)
                     .trimCharactersAtStart ("/");

    if (path.isEmpty())
        path = "index.html";

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
        const auto extension = path.fromLastOccurrenceOf (".", false, false).toLowerCase();

        return juce::WebBrowserComponent::Resource { std::vector<std::byte> (bytes, bytes + size),
                                                     mimeFor (extension) };
    }

    return std::nullopt;
}
