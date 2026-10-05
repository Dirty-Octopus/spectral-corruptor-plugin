// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "gui/EditorContent.h"
#include "PresetManager.h"
#include "dsp/TripleBuffer.h"
#include "dsp/FrequencySplit.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <map>

namespace {
std::unique_ptr<SpectralCrrptProcessor> testProcessor()
{
    auto processor = std::make_unique<SpectralCrrptProcessor>();
    if (! processor->getLicense().isActivated())
        throw std::runtime_error ("Processor regressions require your own working licensing backend");
    return processor;
}
void pump (int milliseconds = 30)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}
template <typename Predicate> bool waitForUI (Predicate ready)
{
    // CI desktop timers may arrive later than one nominal 30 Hz frame.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
    do { pump (20); if (ready()) return true; }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return false;
}
juce::Button* findButton (juce::Component& component, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::Button*> (&component)) if (b->getButtonText() == text) return b;
    for (auto* child : component.getChildren()) if (auto* found = findButton (*child, text)) return found;
    return nullptr;
}
juce::Component* findID (juce::Component& component, const juce::String& id)
{
    if (component.getComponentID() == id) return &component;
    for (auto* child : component.getChildren()) if (auto* found = findID (*child, id)) return found;
    return nullptr;
}
juce::MouseEvent mouse (juce::Component& component, juce::Point<float> position, bool dragged = false, int modifiers = juce::ModifierKeys::leftButtonModifier)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), position, modifiers,
             1, 0, 0, 0, 0, &component, &component, juce::Time::getCurrentTime(), { 20, 20 }, juce::Time::getCurrentTime(), 1, dragged };
}

void setParam (SpectralCrrptProcessor& p, const juce::String& id, float actual)
{
    auto* param = p.getAPVTS().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (actual));
}
bool samePixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
    return true;
}
}

TEST_CASE ("Channel hover returns to idle without changing selection")
{
    scrr::gui::ChannelTab tab; tab.setChannel (1, true); tab.setButtonText ("CH 2"); tab.setSize (64, 32);
    const auto idle = tab.createComponentSnapshot (tab.getLocalBounds());
    for (int repeat = 0; repeat < 4; ++repeat)
    {
        tab.setState (juce::Button::buttonOver); tab.tick (.04f);
        REQUIRE (! samePixels (idle, tab.createComponentSnapshot (tab.getLocalBounds())));
        REQUIRE (! tab.getToggleState());
        tab.setState (juce::Button::buttonNormal);
        for (int frame = 0; frame < 30; ++frame) tab.tick (.033f);
        REQUIRE (samePixels (idle, tab.createComponentSnapshot (tab.getLocalBounds())));
    }
    tab.setToggleState (true, juce::dontSendNotification);
    const auto selected = tab.createComponentSnapshot (tab.getLocalBounds());
    tab.setState (juce::Button::buttonOver); tab.tick (.1f);
    tab.setState (juce::Button::buttonNormal);
    for (int frame = 0; frame < 30; ++frame) tab.tick (.033f);
    REQUIRE (tab.getToggleState());
    REQUIRE (samePixels (selected, tab.createComponentSnapshot (tab.getLocalBounds())));
}

TEST_CASE ("The entire preset row finishes hover animation without incidental repaints")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    { std::cout << "[SKIP native hover subsection: no display]\n"; return; }
    struct FrameLook : scrr::gui::InstrumentLookAndFeel
    {
        std::map<juce::Button*, juce::Image> frames;
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                                   bool over, bool down) override
        {
            juce::Image frame (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
            juce::Graphics capture (frame);
            InstrumentLookAndFeel::drawButtonBackground (capture, b, colour, over, down);
            frames[&b] = frame; g.drawImageAt (frame, 0, 0);
        }
    } look;
    auto processor = testProcessor();
    juce::Component window; window.setLookAndFeel (&look);
    scrr::gui::PresetBar bar (*processor); window.addAndMakeVisible (bar);
    juce::TextButton activation ("ACTIVATED"); window.addAndMakeVisible (activation);
    window.setBounds (80, 100, 1060, 40); bar.setBounds (0, 0, 920, 40); activation.setBounds (930, 0, 120, 40);
    window.addToDesktop (0); window.setVisible (true);
    std::vector<juce::Button*> buttons;
    for (const auto& text : { "<", ">", "Init", "SAVE", "EXPORT", "IMPORT" })
    { auto* button = findButton (bar, text); REQUIRE (button != nullptr); buttons.push_back (button); }
    buttons.push_back (&activation);
    REQUIRE (waitForUI ([&] { return look.frames.size() == buttons.size(); }));
    const auto idle = look.frames;
    auto atRest = [&] (juce::Button* b) { return samePixels (idle.at (b), look.frames.at (b)); };
    auto lit = [&] (juce::Button* b)
    { return look.frames.at (b).getPixelAt (b->getWidth() - 2, b->getHeight() - 2) == scrr::gui::Theme::white(); };
    for (auto* button : buttons)
    {
        button->setState (juce::Button::buttonOver);
        REQUIRE (waitForUI ([&] { return lit (button); }));
        button->setState (juce::Button::buttonNormal);
        // Inspect only the last frame JUCE actually painted. Taking a new snapshot here
        // would mask the original bug by supplying the missing final repaint ourselves.
        REQUIRE (waitForUI ([&] { return atRest (button); }));
    }
    // Quick sweeps/reversals and press-drag-out must settle independently on every button.
    for (int pass = 0; pass < 3; ++pass)
        for (auto* button : buttons)
        {
            button->setState (juce::Button::buttonOver); pump (20);
            button->setState (juce::Button::buttonNormal); pump (10);
            button->setState (juce::Button::buttonDown); pump (20);
            button->setState (juce::Button::buttonNormal);
        }
    REQUIRE (waitForUI ([&] { for (auto* b : buttons) if (! atRest (b)) return false; return true; }));
    // Modal input blockers (save dialogs, preset popups, file choosers) clear hover,
    // even if the native host has not yet delivered a mouse-exit to the original button.
    auto* save = findButton (bar, "SAVE"); save->setState (juce::Button::buttonOver);
    REQUIRE (waitForUI ([&] { return lit (save); }));
    juce::Component blocker; blocker.setSize (20, 20); blocker.addToDesktop (0); blocker.enterModalState (false);
    REQUIRE (waitForUI ([&] { return atRest (save); }));
    save->setState (juce::Button::buttonNormal); blocker.exitModalState (0); blocker.setVisible (false); pump (30);
    activation.setState (juce::Button::buttonOver); REQUIRE (waitForUI ([&] { return lit (&activation); }));
    activation.setEnabled (false); REQUIRE (waitForUI ([&] { return atRest (&activation); }));
    activation.setEnabled (true);
    save->setState (juce::Button::buttonOver); REQUIRE (waitForUI ([&] { return lit (save); }));
    window.setVisible (false); pump (50); save->setState (juce::Button::buttonNormal); window.setVisible (true);
    REQUIRE (waitForUI ([&] { return atRest (save); }));
    auto temporary = std::make_unique<juce::TextButton> ("Temporary");
    window.addAndMakeVisible (*temporary); temporary->setBounds (0, 0, 100, 40);
    temporary->setState (juce::Button::buttonOver); temporary->createComponentSnapshot (temporary->getLocalBounds());
    temporary.reset(); pump (60); // A timer still pending must not dereference the destroyed button.
    window.setLookAndFeel (nullptr);
}

TEST_CASE ("Channel status LED flashes without audio, stays red when muted and grey when unavailable")
{
    scrr::gui::ChannelTab tab; tab.setChannel (0, false); tab.setButtonText ("CH 1"); tab.setSize (74, 32);
    tab.setToggleState (true, juce::dontSendNotification); tab.setActivity (true, true);
    auto pixels = [&] { return tab.createComponentSnapshot (tab.getLocalBounds()); };
    auto dot = [&] { return pixels().getPixelAt (11, 16); };
    const auto green = dot(); REQUIRE (green == juce::Colour (0xff41f174));
    scrr::params::BeatIndicatorClock clock; scrr::params::HostBeat beat; beat.bpm = 120;
    auto advance = [&] (double seconds) { tab.setBeatPhase (clock.tick (seconds, beat)); tab.tick ((float) seconds); };
    advance (.05); REQUIRE (dot() == green);
    advance (.05); REQUIRE (dot() == green);
    advance (.05); const auto dark = dot(); REQUIRE (dark != green);
    advance (.30); REQUIRE (dot() == dark);
    advance (.06); REQUIRE (dot() == green);
    // The old black bezel occupied this pixel outside the 7 px lamp.
    const auto lit = pixels(); REQUIRE (lit.getPixelAt (6, 16) == lit.getPixelAt (4, 16));
    tab.setActivity (false, true); REQUIRE (dot() == scrr::gui::Theme::red());
    tab.setChannel (0, true); REQUIRE (dot() != pixels().getPixelAt (22, 16));
    for (int i = 0; i < 10; ++i) { tab.tick (.17f); REQUIRE (dot() == scrr::gui::Theme::red()); }
    tab.setActivity (true, false); REQUIRE (dot() == scrr::gui::Theme::inactive());
    for (int i = 0; i < 10; ++i) { tab.tick (.17f); REQUIRE (dot() == scrr::gui::Theme::inactive()); }
    tab.setActivity (true, true); REQUIRE (dot() == green);
}

TEST_CASE ("Host beat clock follows tempo and PPQ then continues while stopped or suspended")
{
    scrr::params::HostBeat beat; scrr::params::BeatIndicatorClock clock;
    REQUIRE (std::abs ((clock.tick (.125, beat)) - (.25)) < .0001);
    beat.bpm = 60; REQUIRE (std::abs ((clock.tick (.25, beat)) - (.5)) < .0001);
    beat.playing = beat.hasTimeline = true; beat.ppq = -1.75; beat.revision = 2;
    REQUIRE (std::abs ((clock.tick (.1, beat)) - (.25)) < .0001);
    REQUIRE (std::abs ((clock.tick (.1, beat)) - (.35)) < .0001); // no fresh audio callback
    beat.ppq = 8; beat.revision += 2; REQUIRE (std::abs ((clock.tick (.1, beat)) - (0)) < .0001);
    beat.playing = false; beat.revision += 2; REQUIRE (std::abs ((clock.tick (.25, beat)) - (.25)) < .0001);
    beat.bpm = 240; REQUIRE (std::abs ((clock.tick (.125, beat)) - (.75)) < .0001);

    struct PlayHead : juce::AudioPlayHead
    {
        PositionInfo position;
        juce::Optional<PositionInfo> getPosition() const override { return position; }
    } playHead;
    playHead.position.setBpm (87); playHead.position.setPpqPosition (3.5); playHead.position.setIsPlaying (true);
    auto p = testProcessor(); p->setPlayHead (&playHead); p->prepareToPlay (48000, 64);
    juce::AudioBuffer<float> block (2, 64); block.clear(); juce::MidiBuffer midi;
    p->processBlock (block, midi); REQUIRE (p->readHostBeat (beat));
    REQUIRE (std::abs (beat.bpm - 87) < .0001); REQUIRE (std::abs (beat.ppq - 3.5) < .0001); REQUIRE (beat.playing); REQUIRE (beat.hasTimeline);
    playHead.position.setBpm (150); playHead.position.setIsPlaying (false);
    p->processBlockBypassed (block, midi); REQUIRE (p->readHostBeat (beat)); REQUIRE (std::abs (beat.bpm - 150) < .0001); REQUIRE (! beat.playing);
    p->setPlayHead (nullptr); p->processBlock (block, midi); REQUIRE (p->readHostBeat (beat));
    REQUIRE (std::abs (beat.bpm - 150) < .0001); REQUIRE (! beat.hasTimeline);
}

TEST_CASE ("Disabling a stage freezes its poster and restores colour on re-enable")
{
    auto p = testProcessor(); p->addModule (1, "SpectralBloom");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    auto* card = dynamic_cast<scrr::gui::EffectCard*> (findID (*editor, "effect-card")); REQUIRE (card != nullptr);
    auto* toggle = findButton (*editor, "ENABLED"); REQUIRE (toggle != nullptr);
    toggle->triggerClick(); pump (120);
    REQUIRE (! (bool) p->getModuleChain (1).getChild (0)["enabled"]);
    card->animate (10, 1); const auto first = card->createComponentSnapshot (card->getLocalBounds());
    card->animate (20, 1); const auto second = card->createComponentSnapshot (card->getLocalBounds());
    REQUIRE (samePixels (first, second));
    REQUIRE (first.getPixelAt (2, 2) == scrr::gui::Theme::inactive());
    toggle->triggerClick(); pump (120);
    card->animate (30, 1); const auto on = card->createComponentSnapshot (card->getLocalBounds());
    REQUIRE (on.getPixelAt (2, 2) == scrr::gui::effectColour ("SpectralBloom"));
    card->animate (31, 1);
    REQUIRE (! samePixels (on, card->createComponentSnapshot (card->getLocalBounds())));
    findButton (*editor, "CH 3")->triggerClick(); pump (100);
    REQUIRE (findButton (*editor, "CH 3")->getToggleState());
    REQUIRE (! findButton (*editor, "CH 1")->getToggleState());
    findButton (*editor, "CH 1")->triggerClick(); pump (100);
    REQUIRE (findButton (*editor, "CH 1")->getToggleState());
    REQUIRE (! findButton (*editor, "CH 3")->getToggleState());
}

TEST_CASE ("Spectrum band colours agree with the channel that processes a tone")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 }) for (int factor : { 1, 2, 4 })
    {
        const scrr::dsp::FrequencySplit split (rate, 4, 0);
        const float cuts[] = { 20, (float) (20 * std::pow (rate / 40, .25)),
                              (float) (20 * std::pow (rate / 40, .5)), (float) (20 * std::pow (rate / 40, .75)), (float) (rate * .5) };
        for (int ch = 0; ch < 4; ++ch)
        {
            const float hz = std::sqrt (cuts[ch] * cuts[ch + 1]);
            const int bins = 4096 * factor + 1;
            const auto weights = split.weightsForBin (scrr::gui::SpectrumScale::bin (hz, bins, rate * factor), bins, rate * factor);
            REQUIRE (weights[(size_t) ch] > .999f);
            REQUIRE (std::abs (scrr::gui::SpectrumScale::frequency (scrr::gui::SpectrumScale::position (hz)) - hz) < 1.0f);
        }
        const scrr::dsp::FrequencySplit blended (rate, 17, 100);
        for (int bin = 0; bin < 4096 * factor + 1; ++bin)
        {
            const auto weights = blended.weightsForBin (bin, 4096 * factor + 1, rate * factor);
            float sum = 0; for (auto weight : weights) { REQUIRE (weight >= 0); sum += weight; }
            REQUIRE (std::abs (sum - 1) < .00001f);
        }
    }
    auto p = testProcessor();
    setParam (*p, scrr::params::id::oversample, 0); setParam (*p, scrr::params::id::fftSize, 0);
    setParam (*p, scrr::params::id::freqSplitNumSplits, 4); p->addModule (2, "FrequencyShift");
    const auto uid = p->getModuleChain (2).getChild (0)["uid"].toString();
    p->setModuleParameter (2, uid, "shift", 187.5); p->prepareToPlay (48000, 128);
    std::complex<double> shifted {}, unshifted {}, untouched {};
    juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
    for (int base = 0; base < 16384; base += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const double t = (double) (base + i) / 48000;
            const float value = (float) (.05 * (std::sin (juce::MathConstants<double>::twoPi * 187.5 * t)
                                             + std::sin (juce::MathConstants<double>::twoPi * 1500 * t)));
            block.setSample (0, i, value); block.setSample (1, i, value);
        }
        p->processBlock (block, midi);
        if (base >= 4096) for (int i = 0; i < 128; ++i)
        {
            const double angle = -juce::MathConstants<double>::twoPi * (double) (base + i) / 48000;
            const double value = block.getSample (0, i);
            shifted += value * std::polar (1.0, angle * 375);
            unshifted += value * std::polar (1.0, angle * 187.5);
            untouched += value * std::polar (1.0, angle * 1500);
        }
    }
    REQUIRE (std::abs (shifted) > std::abs (untouched) * .7);
    REQUIRE (std::abs (unshifted) < std::abs (shifted) * .1);
}

TEST_CASE ("Editor destruction also destroys its entire timer-owning subtree")
{
    for (int i = 0; i < 25; ++i)
    {
        auto processor = testProcessor();
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor());
        REQUIRE (editor->getNumChildComponents() > 0);
        juce::Component::SafePointer<juce::Component> child (editor->getChildComponent (0));
        juce::MemoryBlock state; processor->getStateInformation (state);
        editor.reset();
        REQUIRE (child == nullptr); // Original editor leaked this object and its timer.
        processor.reset();
    }
    pump (300); // Exercise callbacks after the processor has gone away.
}

TEST_CASE ("Preset dialogs and pending callbacks are safe when an editor closes")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    {
        std::cout << "[SKIP native dialog subsection: no display; run in a desktop session]\n";
        return;
    }
    auto p = testProcessor();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    auto* save = findButton (*editor, "SAVE"); REQUIRE (save != nullptr);
    save->triggerClick(); pump (40);
    auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent (0);
    REQUIRE (modal != nullptr);
    juce::Component::SafePointer<juce::Component> closed (modal);
    auto* cancel = findButton (*modal, "Cancel"); REQUIRE (cancel != nullptr);
    cancel->triggerClick(); pump (60);
    REQUIRE (closed == nullptr); // Ending modality alone left an invisible blocker / visible inert window.
    save->triggerClick(); pump (30);
    editor.reset(); p.reset(); pump (40); // Close with a callback still pending.
    REQUIRE (juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
}

TEST_CASE ("Save and Cancel retire the exact dialog and Save writes a readable preset")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    { std::cout << "[SKIP native save subsection: no display]\n"; return; }
    const auto directory = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile ("dialog-regression", "");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    auto processorStorage = testProcessor(); auto& processor = *processorStorage; processor.addModule (1, "SpectralMirror");
    scrr::gui::InstrumentLookAndFeel look;
    scrr::gui::PresetBar bar (processor, directory); bar.setLookAndFeel (&look); bar.setSize (900, 34);
    auto* save = findButton (bar, "SAVE"); REQUIRE (save != nullptr);
    for (const auto* action : { "Cancel", "Save", "Save" })
    {
        save->triggerClick(); pump (40);
        auto* window = dynamic_cast<juce::AlertWindow*> (juce::ModalComponentManager::getInstance()->getModalComponent (0)); REQUIRE (window != nullptr);
        window->getTextEditor ("name")->setText ("Dialog round trip");
        juce::Component::SafePointer<juce::AlertWindow> closed (window);
        auto* button = findButton (*window, action); REQUIRE (button != nullptr); button->triggerClick(); pump (80);
        REQUIRE (closed == nullptr); REQUIRE (juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
        const auto file = directory.getChildFile ("Dialog round trip.scpreset");
        if (juce::String (action) == "Cancel") REQUIRE (! file.exists());
        else
        {
            REQUIRE (file.existsAsFile()); scrr::PresetManager manager (directory);
            auto restoredStorage = testProcessor(); auto& restored = *restoredStorage; REQUIRE (restored.applyPreset (manager.loadFromFile (file)));
            REQUIRE (restored.getModuleChain (1).getChild (0)["type"].toString() == "SpectralMirror");
            REQUIRE (! processor.isPresetDirty());
        }
    }
    // Invalid input must also release the window and leave saved data intact.
    save->triggerClick(); pump (40);
    auto* window = dynamic_cast<juce::AlertWindow*> (juce::ModalComponentManager::getInstance()->getModalComponent (0)); REQUIRE (window != nullptr);
    window->getTextEditor ("name")->setText ("../invalid");
    juce::Component::SafePointer<juce::AlertWindow> closed (window);
    findButton (*window, "Save")->triggerClick(); pump (60); REQUIRE (closed == nullptr);
    REQUIRE (directory.findChildFiles (juce::File::findFiles, false).size() == 1);
    bar.setLookAndFeel (nullptr);
}

TEST_CASE ("Chain drag reorders, cancels outside targets and removes only at the centre")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    { std::cout << "[SKIP native drag subsection: no display]\n"; return; }
    auto pStorage = testProcessor(); auto& p = *pStorage;
    for (const auto* type : { "SpectralMirror", "SpectralBloom", "SpectralComb" }) p.addModule (1, type);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
    editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* content = dynamic_cast<scrr::gui::EditorContent*> (editor->getChildComponent (0)); REQUIRE (content != nullptr);
    const auto firstUid = p.getModuleChain (1).getChild (0)["uid"].toString();
    auto* row = findID (*editor, "stage-" + firstUid); REQUIRE (row != nullptr);
    row->mouseDown (mouse (*row, { 20, 20 }));
    row->mouseDrag (mouse (*row, { 20, 168 }, true)); row->mouseUp (mouse (*row, { 20, 168 }, true)); pump (280);
    REQUIRE (p.getModuleChain (1).getChild (2)["uid"].toString() == firstUid);
    row = findID (*editor, "stage-" + firstUid); REQUIRE (row != nullptr);
    row->mouseDown (mouse (*row, { 20, 20 })); row->mouseDrag (mouse (*row, { -400, -500 }, true)); row->mouseUp (mouse (*row, { -400, -500 }, true)); pump (50);
    REQUIRE (p.getModuleChain (1).getNumChildren() == 3);
    row = findID (*editor, "stage-" + firstUid);
    // Drop in the centre of the detail panel below the macro row.
    const auto target = row->getLocalPoint (content, juce::Point<int> (820, 642)).toFloat();
    row->mouseDown (mouse (*row, { 20, 20 })); row->mouseDrag (mouse (*row, target, true));
    content->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    row->mouseDrag (mouse (*row, target, true)); row->mouseUp (mouse (*row, target, true)); pump (60);
    REQUIRE (p.getModuleChain (1).getNumChildren() == 3);
    row->mouseDown (mouse (*row, { 20, 20 })); row->mouseDrag (mouse (*row, target, true));
    row->mouseUp (mouse (*row, target, true)); pump (70);
    REQUIRE (p.getModuleChain (1).getNumChildren() == 2);
    for (const auto& child : p.getModuleChain (1)) REQUIRE (child["uid"].toString() != firstUid);
    REQUIRE (findButton (*editor, "UP") == nullptr); REQUIRE (findButton (*editor, "REMOVE") == nullptr);
    findButton (*editor, "+ ADD EFFECT")->triggerClick(); pump (30);
    auto* browser = findID (*editor, "effect-browser"); REQUIRE (browser && browser->isVisible());
    findButton (*browser, "FREQUENCY")->triggerClick(); pump (30);
    auto* add = dynamic_cast<juce::Button*> (findID (*browser, "add-SpectralMirror")); REQUIRE (add != nullptr);
    add->triggerClick(); pump (50); REQUIRE (p.getModuleChain (1).getNumChildren() == 3);
    editor.reset(); pump (60); // Queued transition / selection callbacks cannot retain an editor.
}

TEST_CASE ("Host save and restore preserves every chain and metadata")
{
    // Processors contain fixed-capacity spectrum mailboxes. Keep test fixtures
    // on the heap so nested fixtures also run with the Windows 1 MiB stack.
    auto originalStorage = testProcessor(); auto& original = *originalStorage;
    for (int ch = 1; ch <= 4; ++ch) original.addModule (ch, "HarmonicMatch");
    auto uid = original.getModuleChain (2).getChild (0)["uid"].toString();
    original.setModuleParameter (2, uid, "shape", 4);
    original.setModuleParameter (2, uid, "amount", 74.0);
    original.setPresetName (juce::String::fromUTF8 ("Harmonics / 保存测试")); original.setPresetDirty (true);
    setParam (original, scrr::params::id::drywet, 37.0f);
    juce::MemoryBlock data; original.getStateInformation (data);
    auto loadedStorage = testProcessor(); auto& loaded = *loadedStorage; loaded.setStateInformation (data.getData(), (int) data.getSize());
    REQUIRE (loaded.getCurrentPresetName() == original.getCurrentPresetName());
    REQUIRE (loaded.isPresetDirty());
    for (int ch = 1; ch <= 4; ++ch) REQUIRE (loaded.getModuleChain (ch).getNumChildren() == 1);
    REQUIRE ((int) loaded.getModuleChain (2).getChild (0)["shape"] == 4);
    REQUIRE (std::abs ((float) loaded.getModuleChain (2).getChild (0)["amount"] - 74.0f) < .001f);
    REQUIRE (std::abs (loaded.getAPVTS().getRawParameterValue (scrr::params::id::drywet)->load() - 37.0f) < .001f);
    auto detached = loaded.getModuleChain (2); detached.removeAllChildren (nullptr);
    REQUIRE (loaded.getModuleChain (2).getNumChildren() == 1);
}

TEST_CASE ("Legacy and factory presets load and malformed module values are bounded")
{
    auto pStorage = testProcessor(); auto& p = *pStorage;
    juce::ValueTree legacy ("PARAMS"), modules ("modules");
    auto module = scrr::dsp::createDefaultModuleState ("FrequencyShift");
    module.setProperty ("shift", 1.0e20, nullptr); modules.appendChild (module, nullptr); legacy.appendChild (modules, nullptr);
    REQUIRE (p.applyPreset (legacy));
    REQUIRE (p.getModuleChain (1).getNumChildren() == 1);
    REQUIRE ((float) p.getModuleChain (1).getChild (0)["shift"] <= 5000.0f);
    REQUIRE (! p.applyPreset (juce::ValueTree ("UNRELATED")));
    REQUIRE (p.getModuleChain (1).getNumChildren() == 1);
    for (const auto& entry : scrr::factoryPresets::getEntries())
    {
        auto xml = juce::XmlDocument::parse (entry.data); REQUIRE (xml != nullptr);
        REQUIRE (p.applyPreset (juce::ValueTree::fromXml (*xml)));
        juce::MemoryBlock blob; p.getStateInformation (blob); p.setStateInformation (blob.getData(), (int) blob.getSize());
    }
}

TEST_CASE ("Concurrent audio, host saves, state loads and rack edits remain finite")
{
    auto pStorage = testProcessor(); auto& p = *pStorage; p.prepareToPlay (48000, 128); p.addModule (1, "SpectralContrast");
    p.assignMacro (0, 1, p.getModuleChain (1).getChild (0)["uid"].toString(), "amount");
    std::atomic<bool> done { false }, finite { true }, valid { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 257); juce::MidiBuffer midi;
        while (! done.load())
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i) buffer.setSample (c, i, .02f * std::sin ((float) i * .04f));
            p.processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i) if (! std::isfinite (buffer.getSample (c, i))) finite.store (false);
        }
    });
    std::thread host ([&]
    {
        for (int i = 0; i < 500; ++i)
        {
            setParam (p, scrr::params::id::macros[0], (float) (i % 101));
            juce::MemoryBlock state; p.getStateInformation (state);
            if (state.getSize() == 0) valid.store (false);
            if (i % 10 == 0) p.setStateInformation (state.getData(), (int) state.getSize());
        }
    });
    for (int i = 0; i < 200; ++i)
    {
        auto chain = p.getModuleChain (1);
        if (chain.getNumChildren() > 0) p.setModuleParameter (1, chain.getChild (0)["uid"].toString(), "amount", (double) (i % 100));
        p.setPresetName ("Stress " + juce::String (i));
        if (i % 20 == 0) { auto e = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor()); pump (10); }
    }
    host.join(); done.store (true); audio.join();
    REQUIRE (finite.load()); REQUIRE (valid.load()); pump();
}

TEST_CASE ("Spectrum mailbox survives fast writes, changing FFT sizes and slow reads")
{
    auto buffer = std::make_unique<scrr::dsp::TripleBuffer>();
    std::atomic<bool> done { false }, coherent { true };
    std::thread writer ([&]
    {
        std::vector<float> frame (scrr::dsp::TripleBuffer::capacity);
        for (int frameNo = 1; frameNo <= 30000; ++frameNo)
        {
            const size_t count = frameNo % 2 == 0 ? 513 : 16385;
            std::fill (frame.begin(), frame.end(), (float) frameNo);
            buffer->resize (count); buffer->write (frame.data(), count, (double) frameNo);
        }
        done.store (true);
    });
    std::vector<float> received; double rate = 0; int reads = 0;
    while (! done.load())
    {
        if (! buffer->read (received, &rate)) continue;
        ++reads;
        for (auto value : received) if (std::abs (value - (float) rate) > .01f) coherent.store (false);
    }
    writer.join(); REQUIRE (reads > 0); REQUIRE (coherent.load());
}

TEST_CASE ("Engine runs every FFT size and oversampling choice with oversized host blocks")
{
    auto pStorage = testProcessor(); auto& p = *pStorage; p.prepareToPlay (48000, 64);
    juce::AudioBuffer<float> buffer (2, 777); juce::MidiBuffer midi;
    for (int os = 0; os < 3; ++os) for (int fft = 0; fft < 4; ++fft)
    {
        setParam (p, scrr::params::id::oversample, (float) os); setParam (p, scrr::params::id::fftSize, (float) fft);
        buffer.clear(); buffer.setSample (0, 0, .1f); p.processBlock (buffer, midi);
        REQUIRE (p.getProcessor().getOversampleFactor() == (1 << os));
        REQUIRE (p.getProcessor().getFFTSize() == (1024 << fft) * (1 << os));
        for (int i = 0; i < 777; ++i) REQUIRE (std::isfinite (buffer.getSample (0, i)));
    }
}

TEST_CASE ("Stage range and wet mix preserve unselected tones through the complete processor")
{
    auto render = [] (float stageMix, const juce::String& type = "FrequencyShift")
    {
        auto p = testProcessor();
        setParam (*p, scrr::params::id::oversample, 0);
        setParam (*p, scrr::params::id::fftSize, 0);
        p->addModule (1, type);
        auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
        if (type == "FrequencyShift") p->setModuleParameter (1, uid, "shift", 375.0);
        else { p->setModuleParameter (1, uid, "pivot", 1687.5); p->setModuleParameter (1, uid, "amount", 100.0); }
        p->setModuleParameter (1, uid, "rangeLow", 1000.0);
        p->setModuleParameter (1, uid, "rangeHigh", 2500.0);
        p->setModuleParameter (1, uid, "moduleMix", stageMix);
        p->prepareToPlay (48000, 128);
        std::vector<float> samples (16384);
        juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
        for (int base = 0; base < (int) samples.size(); base += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const double time = (double) (base + i) / 48000.0;
                const float signal = (float) (.05 * std::sin (juce::MathConstants<double>::twoPi * 187.5 * time)
                                           + .05 * std::sin (juce::MathConstants<double>::twoPi * 1500.0 * time));
                block.setSample (0, i, signal); block.setSample (1, i, signal);
            }
            p->processBlock (block, midi);
            std::copy (block.getReadPointer (0), block.getReadPointer (0) + 128, samples.begin() + base);
        }
        return samples;
    };
    auto magnitude = [] (const std::vector<float>& samples, double hz)
    {
        std::complex<double> sum {};
        for (int i = 4096; i < 16384; ++i)
            sum += (double) samples[(size_t) i] * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * (double) i / 48000.0);
        return std::abs (sum);
    };
    const auto dry = render (0), wet = render (100);
    REQUIRE (std::abs (magnitude (wet, 187.5) / magnitude (dry, 187.5) - 1.0) < .02);
    REQUIRE (magnitude (wet, 1875) > magnitude (dry, 1500) * .8);
    REQUIRE (magnitude (wet, 1500) < magnitude (dry, 1500) * .1);
    REQUIRE (magnitude (dry, 1875) < magnitude (dry, 1500) * .01);
    const auto mirror = render (100, "SpectralMirror");
    REQUIRE (magnitude (mirror, 1875) > magnitude (dry, 1500) * .8);
    REQUIRE (magnitude (mirror, 1500) < magnitude (dry, 1500) * .1);
    REQUIRE (std::abs (magnitude (mirror, 187.5) / magnitude (dry, 187.5) - 1.0) < .02);
}

TEST_CASE ("Preset writing is round-trippable and rejects directory traversal")
{
    scrr::PresetManager manager; auto pStorage = testProcessor(); auto& p = *pStorage;
    auto file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile ("spectral-regression", ".scpreset");
    auto result = manager.saveToFile (file, p.copyPresetState());
    if (result.failed()) std::cerr << file.getFullPathName() << ": " << result.getErrorMessage() << '\n';
    REQUIRE (result.wasOk());
    REQUIRE (manager.loadFromFile (file).hasType ("PARAMS"));
    REQUIRE (manager.saveToUser ("../escape", p.copyPresetState()).failed());
    file.deleteFile();
}


TEST_CASE ("Eight host macros add signed modulation around an editable base")
{
    using namespace scrr::params;
    auto p = testProcessor();
    for (const auto& id : id::macros)
    { auto* param = p->getAPVTS().getParameter (id); REQUIRE (param != nullptr); REQUIRE (param->isAutomatable()); }
    p->addModule (1, "FrequencyShift"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 125.0); p->assignMacro (0, 1, uid, "shift");
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - 125) < .01);
    auto m = p->getMacroMappings()[0];
    for (bool bipolar : { false, true }) for (double depth : { .1, -.1 })
    {
        m.setDepth (depth, bipolar); p->updateMacroMapping (m);
        for (int stop = 0; stop < 3; ++stop)
        {
            setParam (*p, id::macros[0], (float) stop * 50);
            const double expected = 125 + (bipolar ? stop - 1 : stop * .5) * depth * 10000;
            REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - expected) < .01);
        }
    }
    m.setDepth (.1, false); p->updateMacroMapping (m);
    p->assignMacro (1, 1, uid, "shift"); p->assignMacro (1, 1, uid, "shift");
    REQUIRE (p->getMacroMappings().size() == 2);
    auto second = p->getMacroMappings()[1]; second.setDepth (.05, false); p->updateMacroMapping (second);
    setParam (*p, id::macros[0], 100); setParam (*p, id::macros[1], 100);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - 1625) < .01);
    p->setModuleParameter (1, uid, "shift", 250.0);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - 1750) < .01);
    p->removeMacroMapping (1, uid, "shift", 0);
    REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - 750) < .01);
    p->removeMacroMapping (1, uid, "shift");
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 250) < .01);
    p->assignMacro (0, 0, {}, juce::Identifier (id::drywet)); m = p->getMacroMappings()[0]; m.setDepth (-1, false); p->updateMacroMapping (m);
    setParam (*p, id::macros[0], 25);
    REQUIRE (std::abs (p->getEffectiveValue (0, {}, juce::Identifier (id::drywet), 100) - 75) < .01);
    p->removeMacroMapping (0, {}, juce::Identifier (id::drywet));
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (id::drywet)->load() - 100) < .01);
    p->assignMacro (8, 1, uid, "shift"); p->assignMacro (0, 1, uid, "type"); p->assignMacro (0, 0, {}, juce::Identifier (id::macros[0]));
    REQUIRE (p->getMacroMappings().empty());
}

TEST_CASE ("Macro mappings restore with fresh module IDs and reject malformed targets")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (2, "FrequencyShift"); p->addModule (2, "SpectralBloom");
    const auto uid = p->getModuleChain (2).getChild (0)["uid"].toString();
    p->setModuleParameter (2, uid, "shift", 700.0); p->assignMacro (7, 2, uid, "shift");
    auto m = p->getMacroMappings()[0]; m.startOffset = -.08; m.endOffset = .05; m.mode = 2; p->updateMacroMapping (m);
    setParam (*p, id::macros[7], 75); p->moveModule (2, 0, 1);
    juce::MemoryBlock state; p->getStateInformation (state);
    auto restored = testProcessor(); restored->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (restored->getMacroMappings().size() == 1);
    const auto newUid = restored->getModuleChain (2).getChild (1)["uid"].toString(); REQUIRE (newUid != uid);
    const auto result = restored->getMacroMappings()[0]; REQUIRE (result.uid == newUid); REQUIRE (std::abs (result.centre - 700) < .01);
    REQUIRE (std::abs (restored->getEffectiveValue (2, newUid, "shift", 0) - 950) < .01);
    auto preset = p->copyPresetState(); auto maps = preset.getChildWithName ("macroMappings");
    auto invalid = maps.getChild (0).createCopy(); invalid.setProperty ("macro", 99, nullptr); maps.appendChild (invalid, nullptr);
    invalid = maps.getChild (0).createCopy(); invalid.setProperty ("parameter", "type", nullptr); maps.appendChild (invalid, nullptr);
    maps.appendChild (maps.getChild (0).createCopy(), nullptr);
    maps.getChild (0).setProperty ("startOffset", -1.0e20, nullptr); maps.getChild (0).setProperty ("endOffset", 1.0e20, nullptr);
    maps.getChild (0).setProperty ("centre", std::numeric_limits<double>::quiet_NaN(), nullptr);
    REQUIRE (restored->applyPreset (preset)); REQUIRE (restored->getMacroMappings().size() == 1);
    const auto bounded = restored->getMacroMappings()[0];
    REQUIRE (std::isfinite (bounded.centre)); REQUIRE (std::abs (bounded.low - bounded.minimum) < .01); REQUIRE (std::abs (bounded.high - bounded.maximum) < .01);
    restored->removeModule (2, 1); REQUIRE (restored->getMacroMappings().empty());
    auto legacy = preset.createCopy(); legacy.removeChild (legacy.getChildWithName ("macroMappings"), nullptr);
    for (const auto& id : id::macros) legacy.removeChild (legacy.getChildWithProperty ("id", id), nullptr);
    REQUIRE (restored->applyPreset (legacy)); REQUIRE (restored->getMacroMappings().empty());
    for (auto value : restored->getMacroValues()) REQUIRE (std::abs (value) < .001f);
}

TEST_CASE ("Macro automation changes audio without an editor or mutating saved base parameters")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::fftSize, 0);
    p->addModule (1, "FrequencyShift"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 0.0); p->assignMacro (0, 1, uid, "shift");
    auto m = p->getMacroMappings()[0]; m.setDepth (.0375, false); p->updateMacroMapping (m);
    p->prepareToPlay (48000, 128);
    auto amplitude = [&] (float position)
    {
        setParam (*p, id::macros[0], position);
        juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi;
        std::complex<double> original {}, shifted {};
        for (int base = 0; base < 16384; base += 128)
        {
            for (int i = 0; i < 128; ++i) for (int c = 0; c < 2; ++c)
                audio.setSample (c, i, .05f * (float) std::sin (juce::MathConstants<double>::twoPi * 1500 * (base + i) / 48000));
            p->processBlock (audio, midi);
            if (base < 4096) continue;
            for (int i = 0; i < 128; ++i)
            {
                const double t = juce::MathConstants<double>::twoPi * (base + i) / 48000;
                original += (double) audio.getSample (0, i) * std::polar (1.0, -1500 * t);
                shifted += (double) audio.getSample (0, i) * std::polar (1.0, -1875 * t);
            }
        }
        return std::pair<double, double> { std::abs (original), std::abs (shifted) };
    };
    const auto original = amplitude (0), shifted = amplitude (100), returned = amplitude (0);
    REQUIRE (original.first > 100); REQUIRE (original.second < original.first * .01);
    REQUIRE (shifted.second > original.first * .8); REQUIRE (shifted.first < original.first * .1);
    REQUIRE (returned.first > original.first * .98);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"]) < .001);
    REQUIRE (std::abs ((double) p->copyPresetState().getChildWithName ("ch1").getChild (0)["shift"]) < .001);
}

TEST_CASE ("Macro controls, context assignment and range editing survive editor teardown")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    { std::cout << "[SKIP native macro subsection: no display]\n"; return; }
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 125.0);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    editor->addToDesktop (0); editor->setVisible (true); editor->setSize (1040, 780); pump (350);
    auto* bar = findID (*editor, "macro-bar"); REQUIRE (bar != nullptr);
    for (int i = 1; i <= 8; ++i)
    {
        auto* knob = dynamic_cast<juce::Slider*> (findID (*editor, "macro-" + juce::String (i))); REQUIRE (knob != nullptr);
        REQUIRE (bar->getLocalBounds().contains (knob->getBounds()));
        knob->setValue (i * 10, juce::sendNotificationSync);
        REQUIRE (std::abs (p->getMacroValues()[(size_t) i - 1] - (float) i * .1f) < .001f);
    }
    auto getSlider = [&]() { return dynamic_cast<scrr::gui::ParameterControl*> (findID (*editor, "shift"))->getAssignableSlider(); };
    auto* slider = getSlider(); REQUIRE (slider != nullptr);
    auto right = mouse (*slider, { 10, 10 }, false, juce::ModifierKeys::rightButtonModifier);
    slider->mouseDown (right);
    auto* menu = juce::Component::getCurrentlyModalComponent(); REQUIRE (menu != nullptr);
    menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey)); menu->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); pump (100);
    REQUIRE (p->getMacroMappings().size() == 1); REQUIRE (p->getMacroMappings()[0].macro == 0);
    REQUIRE (waitForUI ([&] { return getSlider()->isMacroControlled(); }));
    dynamic_cast<juce::Button*> (findID (*editor, "macro-map-1"))->triggerClick(); pump (40);
    auto* panel = findID (*editor, "macro-mappings"); REQUIRE (panel && panel->isVisible());
    auto* low = dynamic_cast<juce::TextEditor*> (findID (*panel, "map-low")); REQUIRE (low != nullptr);
    low->setText ("-10"); low->onReturnKey();
    REQUIRE (waitForUI ([&] { return std::abs (p->getMacroMappings()[0].startOffset + .1) < .0001; }));
    auto* centre = dynamic_cast<juce::TextEditor*> (findID (*panel, "map-centre")); REQUIRE (centre != nullptr);
    centre->setText ("350"); centre->onReturnKey();
    REQUIRE (waitForUI ([&] { return std::abs (p->getMacroMappings()[0].centre - 350) < .01; }));
    auto* mode = dynamic_cast<juce::ComboBox*> (findID (*panel, "map-mode")); REQUIRE (mode != nullptr);
    mode->setSelectedItemIndex (1, juce::sendNotificationSync);
    REQUIRE (waitForUI ([&] { return p->getMacroMappings()[0].mode == 2; }));
    setParam (*p, scrr::params::id::macros[0], 50);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - 350) < .01);
    const bool refreshed = waitForUI ([&] { return std::abs (getSlider()->getValue() - 350) < .01; });
    if (! refreshed) std::cerr << "Macro UI value=" << getSlider()->getValue() << ", editor showing=" << editor->isShowing() << '\n';
    REQUIRE (refreshed);
    REQUIRE (std::abs (p->getMacroMappings()[0].startOffset + .1) < .0001);
    findButton (*panel, "UNMAP")->triggerClick();
    REQUIRE (waitForUI ([&] { return p->getMacroMappings().empty() && ! getSlider()->isMacroControlled(); }));
    REQUIRE (std::abs (getSlider()->getValue() - 350) < .01);
    findButton (*panel, "CLOSE  x")->triggerClick(); pump (50); REQUIRE (! panel->isVisible());
    getSlider()->mappingMenu(); pump (30); editor.reset(); pump (100);
    REQUIRE (juce::Component::getCurrentlyModalComponent() == nullptr);
}

TEST_CASE ("Stage transfers preserve identity on moves and isolate duplicate and clipboard mappings")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->addModule (2, "Reverb");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 200.0); p->assignMacro (0, 1, uid, "shift"); p->assignMacro (1, 1, uid, "shift");
    auto m = p->getMacroMappings()[0]; m.setDepth (.05, true); p->updateMacroMapping (m);
    REQUIRE (p->transferModule (1, uid, 2, -1, false) == uid);
    REQUIRE (p->getModuleChain (1).getNumChildren() == 0);
    REQUIRE (p->getModuleChain (2).getChild (0)["type"].toString() == "FrequencyShift");
    REQUIRE (p->getModuleChain (2).getChild (1)["type"].toString() == "Reverb");
    for (const auto& mapping : p->getMacroMappings()) REQUIRE (mapping.channel == 2);
    const auto copy = p->transferModule (2, uid, 3, -1, true); REQUIRE (copy.isNotEmpty()); REQUIRE (copy != uid);
    REQUIRE (p->getMacroMappings().size() == 4);
    p->setModuleParameter (3, copy, "shift", -300.0);
    REQUIRE (std::abs ((double) p->getModuleChain (2).getChild (0)["shift"] - 200) < .01);
    const auto clipboard = p->copyModule (2, uid);
    auto other = testProcessor(); const auto pasted = other->pasteModule (4, clipboard); REQUIRE (pasted.isNotEmpty()); REQUIRE (pasted != uid);
    REQUIRE (other->getMacroMappings().size() == 2);
    REQUIRE (std::abs (other->getMacroMappings()[0].endOffset - .05) < 1.0e-6);
    REQUIRE (other->pasteModule (4, "<SPECTRAL_EFFECT><module type='Unknown'/></SPECTRAL_EFFECT>").isEmpty());
    for (int i = 0; i < 32; ++i) p->addModule (4, "Delay");
    REQUIRE (p->transferModule (2, uid, 4, -1, false).isEmpty());
    REQUIRE (p->getModuleChain (2).getChildWithProperty ("uid", uid).isValid());
}

TEST_CASE ("Extended FFT sizes run with every oversampling factor and old projects use legacy automation")
{
    using namespace scrr::params;
    auto p = testProcessor(); juce::MidiBuffer midi;
    for (int size = 0; size < 9; ++size) for (int os = 0; os < 3; ++os)
    {
        const int samples = 128 << size;
        setParam (*p, id::fftSizeExtended, (float) size + 1); setParam (*p, id::oversample, (float) os);
        p->prepareToPlay (48000, 257);
        REQUIRE (p->getProcessor().getFFTSize() == samples * (1 << os));
        float peak = 0;
        juce::AudioBuffer<float> audio (2, 257);
        for (int base = 0; base < samples * 3 + 1024; base += 257)
        {
            for (int i = 0; i < 257; ++i) for (int c = 0; c < 2; ++c) audio.setSample (c, i, .05f * std::sin ((float) (base + i) * .11f));
            p->processBlock (audio, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 257; ++i) { REQUIRE (std::isfinite (audio.getSample (c, i))); peak = juce::jmax (peak, std::abs (audio.getSample (c, i))); }
        }
        REQUIRE (peak > .02f); REQUIRE (peak < .08f);
    }
    auto legacy = p->copyPresetState(); legacy.removeChild (legacy.getChildWithProperty ("id", id::fftSizeExtended), nullptr);
    for (const auto& id : id::channelEnabled) legacy.removeChild (legacy.getChildWithProperty ("id", id), nullptr);
    for (const auto& id : id::channelEnabled) setParam (*p, id, 0);
    REQUIRE (p->applyPreset (legacy));
    REQUIRE (p->getAPVTS().getRawParameterValue (id::fftSizeExtended)->load() < .5f);
    for (const auto& id : id::channelEnabled) REQUIRE (p->getAPVTS().getRawParameterValue (id)->load() > .5f);
    setParam (*p, id::fftSize, 2); p->prepareToPlay (48000, 128); REQUIRE (p->getProcessor().getFFTSize() == 4096 * 4);
}

TEST_CASE ("Legacy absolute Macro mappings migrate without changing their endpoints")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "shift"); const auto snapshot = p->copyPresetState();
    for (int mode = 0; mode < 4; ++mode)
    {
        auto old = snapshot.createCopy(); auto tree = old.getChildWithName ("macroMappings").getChild (0);
        tree.removeProperty ("version", nullptr); tree.removeProperty ("startOffset", nullptr); tree.removeProperty ("endOffset", nullptr);
        tree.setProperty ("low", -1000, nullptr); tree.setProperty ("high", 2000, nullptr); tree.setProperty ("centre", 350, nullptr); tree.setProperty ("mode", mode, nullptr);
        REQUIRE (p->applyPreset (old)); uid = p->getModuleChain (1).getChild (0)["uid"].toString();
        for (int stop = 0; stop < 3; ++stop)
        {
            setParam (*p, scrr::params::id::macros[0], (float) stop * 50);
            const bool reverse = mode == 1 || mode == 3;
            const double expected = stop == 1 ? (mode < 2 ? 500 : 350) : ((stop == 0) != reverse ? -1000 : 2000);
            REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0) - expected) < .01);
        }
    }
}

TEST_CASE ("Channel mute and GENERIC routing do not affect other frequency channels")
{
    auto render = [] (bool muted, bool emptyChannelEffect, int effectChannel)
    {
        auto p = testProcessor(); using namespace scrr::params;
        setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 4);
        setParam (*p, id::channelEnabled[0], muted ? 0 : 1);
        if (emptyChannelEffect)
        {
            p->addModule (effectChannel, "Distortion");
            const auto uid = p->getModuleChain (effectChannel).getChild (0)["uid"].toString();
            p->setModuleParameter (effectChannel, uid, "drive", 36.0); p->setModuleParameter (effectChannel, uid, "mode", 2);
        }
        p->prepareToPlay (48000, 128); juce::AudioBuffer<float> b (2, 128); juce::MidiBuffer midi;
        std::vector<float> output;
        for (int base = 0; base < 8192; base += 128)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) b.setSample (c, i, .1f * std::sin ((float) (base + i) * .2f));
            p->processBlock (b, midi);
            if (base >= 4096) output.insert (output.end(), b.getReadPointer (0), b.getReadPointer (0) + 128);
        }
        return output;
    };
    const auto dry = render (false, false, 1), unaffected = render (false, true, 2), affected = render (false, true, 1), muted = render (true, true, 1);
    double difference = 0;
    for (size_t i = 0; i < dry.size(); ++i)
    {
        REQUIRE (std::abs (dry[i] - unaffected[i]) < 1.0e-6f); REQUIRE (std::abs (muted[i]) < 1.0e-7f);
        difference += std::abs (dry[i] - affected[i]);
    }
    REQUIRE (difference > 10);
}

TEST_CASE ("GENERIC delay modes place distinct taps and ping-pong alternates channels")
{
    for (int mode = 0; mode < 3; ++mode)
    {
        scrr::dsp::GenericDelay delay; auto state = scrr::dsp::createDefaultModuleState ("Delay");
        state.setProperty ("mode", mode, nullptr); state.setProperty ("time", 30.0, nullptr); state.setProperty ("feedback", 50.0, nullptr); state.setProperty ("mix", 100.0, nullptr);
        delay.updateParameters (state); delay.prepare (48000, 513);
        juce::AudioBuffer<float> audio (2, 5000); audio.clear(); audio.setSample (0, 0, 1); audio.setSample (1, 0, 1);
        delay.processBuffer (audio, 48000);
        if (mode == 0) { REQUIRE (audio.getSample (0, 1440) > .99f); REQUIRE (audio.getSample (0, 2880) > .49f); }
        if (mode == 1) { REQUIRE (audio.getSample (0, 480) > .54f); REQUIRE (audio.getSample (0, 960) > .29f); }
        if (mode == 2) { REQUIRE (audio.getSample (0, 1440) > .99f); REQUIRE (std::abs (audio.getSample (1, 1440)) < .001f); REQUIRE (audio.getSample (1, 2880) > .49f); }
        delay.reset(); audio.clear(); delay.processBuffer (audio, 48000); REQUIRE (audio.getMagnitude (0, 5000) < 1.0e-7f);
    }
}

TEST_CASE ("GENERIC reverb modes have distinct decaying tails and compressor is stereo linked")
{
    std::array<double, 3> signature {};
    for (int mode = 0; mode < 3; ++mode)
    {
        scrr::dsp::GenericReverb reverb; reverb.prepare (48000, 513);
        auto s = scrr::dsp::createDefaultModuleState ("Reverb"); s.setProperty ("mode", mode, nullptr); s.setProperty ("mix", 100, nullptr); s.setProperty ("decay", .5, nullptr); reverb.updateParameters (s);
        juce::AudioBuffer<float> audio (2, 48000 * 2); audio.clear(); audio.setSample (0, 0, .5f); audio.setSample (1, 0, .5f);
        reverb.processBuffer (audio, 48000);
        double early = 0, late = 0;
        for (int i = 0; i < 96000; ++i) { const float value = audio.getSample (0, i); REQUIRE (std::isfinite (value)); if (i < 24000) early += value * value; if (i > 72000) late += value * value; signature[(size_t) mode] += std::abs (value) * (i % 137); }
        REQUIRE (early > .001); REQUIRE (late < early * .001);
        reverb.reset(); audio.clear(); reverb.processBuffer (audio, 48000); REQUIRE (audio.getMagnitude (0, 96000) < 1.0e-7f);
    }
    REQUIRE (std::abs (signature[0] - signature[1]) > .01); REQUIRE (std::abs (signature[1] - signature[2]) > .01);
    scrr::dsp::GenericCompressor compressor; compressor.prepare (48000, 513); auto s = scrr::dsp::createDefaultModuleState ("Compressor"); compressor.updateParameters (s);
    juce::AudioBuffer<float> audio (2, 48000);
    for (int i = 0; i < 48000; ++i) { audio.setSample (0, i, .8f); audio.setSample (1, i, .2f); }
    compressor.processBuffer (audio, 48000);
    REQUIRE (audio.getSample (0, 47999) < .3f);
    REQUIRE (std::abs (audio.getSample (0, 47999) / audio.getSample (1, 47999) - 4) < .001f);
}

TEST_CASE ("GENERIC distortion shapes remain distinct and finite at maximum drive")
{
    std::array<std::vector<float>, 4> outputs;
    for (int mode = 0; mode < 4; ++mode)
    {
        scrr::dsp::GenericDistortion distortion; distortion.prepare (192000, 513);
        auto s = scrr::dsp::createDefaultModuleState ("Distortion"); s.setProperty ("mode", mode, nullptr); s.setProperty ("drive", 36, nullptr); distortion.updateParameters (s);
        juce::AudioBuffer<float> audio (2, 4096);
        for (int i = 0; i < 4096; ++i) for (int c = 0; c < 2; ++c) audio.setSample (c, i, std::sin ((float) i * .07f) * .5f);
        distortion.processBuffer (audio, 192000);
        outputs[(size_t) mode].assign (audio.getReadPointer (0), audio.getReadPointer (0) + 4096);
        for (auto value : outputs[(size_t) mode]) { REQUIRE (std::isfinite (value)); REQUIRE (std::abs (value) < 2); }
    }
    for (size_t mode = 1; mode < 4; ++mode)
    { double difference = 0; for (size_t i = 0; i < 4096; ++i) difference += std::abs (outputs[mode][i] - outputs[mode - 1][i]); REQUIRE (difference > 1); }
}

TEST_CASE ("LFO waveform, tempo sync, restart and envelope rise-fall are deterministic")
{
    scrr::dsp::ModulationEngine engine;
    scrr::params::ModulationSource source; source.uid = "lfo-test"; source.shape = 2; source.rate = 1;
    engine.configure ({ source }); engine.prepare (48000);
    juce::AudioBuffer<float> input (2, 480); input.clear();
    for (int i = 0; i < 26; ++i) engine.process (input, 0, 480, {}, 120, false, 0, false);
    REQUIRE (std::abs (engine.value (0) - .25f) < .0001f);
    source.sync = true; source.division = 5; engine.configure ({ source });
    engine.process (input, 0, 480, {}, 120, true, .25, true); REQUIRE (std::abs (engine.value (0) - .25f) < .0001f);
    source.retrigger = true; engine.configure ({ source });
    engine.process (input, 0, 480, {}, 120, false, 7, true);
    engine.process (input, 0, 480, {}, 120, true, 7, true); REQUIRE (std::abs (engine.value (0)) < .0001f);
    source.envelope = true; source.attack = 10; source.release = 100; source.input = 0;
    engine.configure ({ source }); engine.prepare (48000);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 480; ++i) input.setSample (c, i, 1);
    engine.process (input, 0, 480, {}, 120, false, 0, false);
    REQUIRE (std::abs (engine.value (0) - .6321f) < .001f);
    input.clear(); engine.process (input, 0, 480, {}, 120, false, 0, false);
    REQUIRE (engine.value (0) > .56f); REQUIRE (engine.value (0) < .58f);
    source.input = 3; engine.configure ({ source }); engine.prepare (48000);
    engine.process (input, 0, 480, { 0, 0, .5f, 0 }, 120, false, 0, false); REQUIRE (std::abs (engine.value (0) - .316f) < .001f);
    source.input = 5; engine.configure ({ source }); engine.prepare (48000);
    engine.process (input, 0, 480, { 1, 1, 1, 1 }, 120, false, 0, false); REQUIRE (engine.value (0) < 1.0e-7f);
}

TEST_CASE ("Modulation sources save curves and routes while source removal prunes only its routes")
{
    auto p = testProcessor(); p->addModule (1, "SpectralContrast");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto lfo = p->addModulator (false), env = p->addModulator (true);
    auto source = p->getModulators()[0]; source.shape = 5; source.points = { { 0, .1f }, { .2f, .9f }, { .8f, .3f }, { 1, .1f } }; p->updateModulator (source);
    p->assignMacro (0, 1, uid, "amount"); p->assignModulator (lfo, 1, uid, "amount"); p->assignModulator (env, 1, uid, "amount");
    REQUIRE (p->getMacroMappings().size() == 3);
    auto mapping = p->getMacroMappings()[1]; mapping.setDepth (.3, true); p->updateMacroMapping (mapping);
    juce::MemoryBlock state; p->getStateInformation (state);
    auto restored = testProcessor(); restored->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (restored->getModulators().size() == 2); REQUIRE (restored->getModulators()[0].points.size() == 4);
    REQUIRE (restored->getMacroMappings().size() == 3); REQUIRE (restored->getMacroMappings()[1].sourceUid == lfo);
    const auto newUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (restored->getMacroMappings()[1].uid == newUid);
    auto other = testProcessor(); REQUIRE (other->pasteModule (3, restored->copyModule (1, newUid)).isNotEmpty());
    REQUIRE (other->getModulators().size() == 2); REQUIRE (other->getMacroMappings().size() == 3);
    restored->removeModulator (lfo);
    REQUIRE (restored->getModulators().size() == 1); REQUIRE (restored->getMacroMappings().size() == 2);
    REQUIRE (restored->getMacroMappings()[1].sourceUid == env); REQUIRE (restored->getMacroMappings()[1].valueIndex() == 8);
    auto legacy = restored->copyPresetState(); legacy.removeChild (legacy.getChildWithName ("modulators"), nullptr);
    REQUIRE (restored->applyPreset (legacy)); REQUIRE (restored->getModulators().empty()); REQUIRE (restored->getMacroMappings().size() == 1);
}

TEST_CASE ("Random modulation holds values, interpolates smoothly and follows a repeatable timeline")
{
    scrr::params::ModulationSource source; source.uid = "random-test"; source.random = true; source.seed = 42;
    REQUIRE (std::abs (source.valueAt (.01) - source.valueAt (.99)) < 1.0e-7f); REQUIRE (std::abs (source.valueAt (.01) - source.valueAt (1.01)) > .001f);
    for (double p : { -123.7, -1.0, 0.0, .5, 1.0, 10000000000.5 })
    { REQUIRE (std::isfinite (source.valueAt (p))); REQUIRE (source.valueAt (p) >= 0); REQUIRE (source.valueAt (p) <= 1); }
    const auto first = source.valueAt (0), next = source.valueAt (1); source.shape = 1;
    REQUIRE (std::abs (source.valueAt (.5) - (first + next) * .5f) < .0001f);
    REQUIRE (std::abs (source.valueAt (.999999) - source.valueAt (1.000001)) < .0001f);
    const auto restored = scrr::params::ModulationSource::read (source.state()); REQUIRE (restored.random); REQUIRE (restored.seed == 42);
    scrr::dsp::ModulationEngine a, b; a.configure ({ source }); b.configure ({ source }); a.prepare (48000); b.prepare (48000);
    juce::AudioBuffer<float> input (2, 480); input.clear();
    for (int i = 0; i < 150; ++i) a.process (input, 0, 480, {}, 120, false, 0, false);
    for (int i = 0; i < 300; ++i) b.process (input, 0, 240, {}, 120, false, 0, false);
    a.process (input, 0, 0, {}, 120, false, 0, false); b.process (input, 0, 0, {}, 120, false, 0, false);
    REQUIRE (std::abs (a.value (0) - b.value (0)) < .00001f); REQUIRE (std::abs (a.value (0) - source.valueAt (1.5)) < .00001f);
    source.sync = true; source.division = 5; a.configure ({ source });
    a.process (input, 0, 480, {}, 120, true, 3.25, true); const float atBeat = a.value (0);
    a.process (input, 0, 480, {}, 120, true, 50.3, true);
    a.process (input, 0, 480, {}, 120, true, 3.25, true); REQUIRE (std::abs (a.value (0) - atBeat) < 1.0e-7f);
    source.retrigger = true; a.configure ({ source }); a.process (input, 0, 480, {}, 120, false, 7, true);
    a.process (input, 0, 480, {}, 120, true, 7, true); REQUIRE (std::abs (a.value (0) - source.valueAt (0)) < .00001f);
}

TEST_CASE ("LFO custom points and hollow curvature handles edit and persist without modifier gestures")
{
    scrr::gui::ModulationCurve curve; curve.setSize (416, 216); curve.source.shape = 5;
    curve.source.points = { { 0, 0 }, { 1, 1 } };
    curve.mouseDoubleClick (mouse (curve, { 208, 168 })); REQUIRE (curve.source.points.size() == 3);
    curve.mouseDoubleClick (mouse (curve, { 208, 168 })); REQUIRE (curve.source.points.size() == 2);
    curve.mouseDoubleClick (mouse (curve, { 208, 168 }));
    curve.mouseDown (mouse (curve, { 208, 168 }, false, juce::ModifierKeys::rightButtonModifier)); REQUIRE (curve.source.points.size() == 2);
    // Linear midpoint is (208, 108). Drag its hollow handle upwards.
    curve.mouseDown (mouse (curve, { 208, 108 })); curve.mouseDrag (mouse (curve, { 208, 48 }, true)); curve.mouseUp (mouse (curve, { 208, 48 }));
    REQUIRE (curve.source.points[0].curve > .39f); REQUIRE (curve.source.valueAt (.5) > .85f);
    const auto saved = scrr::params::ModulationSource::read (curve.source.state());
    REQUIRE (std::abs (saved.points[0].curve - .4f) < .001f); REQUIRE (std::abs (saved.valueAt (.5) - curve.source.valueAt (.5)) < .00001f);
    const auto before = curve.source.points[0].curve;
    const int alt = juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier;
    curve.mouseDown (mouse (curve, { 108, 110 }, false, alt)); curve.mouseDrag (mouse (curve, { 108, 40 }, true, alt)); curve.mouseUp (mouse (curve, { 108, 40 }));
    REQUIRE (std::abs (curve.source.points[0].curve - before) < 1.0e-7f);
    curve.mouseDoubleClick (mouse (curve, { 8, 208 })); REQUIRE (curve.source.points.size() == 2);
    auto malformed = saved.state(); malformed.getChild (0).setProperty ("curve", std::numeric_limits<double>::infinity(), nullptr);
    REQUIRE (std::abs (scrr::params::ModulationSource::read (malformed).points[0].curve) < 1.0e-7f);
}

TEST_CASE ("New assignments have visible depth and retired seed controls migrate to Random sources")
{
    auto p = testProcessor(); p->addModule (1, "BinShuffle"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto random = p->addModulator (false, true); p->assignModulator (random, 1, uid, "seed");
    const auto mapping = p->getMacroMappings()[0]; REQUIRE (mapping.high > mapping.low); REQUIRE (std::abs (mapping.endOffset - .2) < .00001);
    p->assignMacro (0, 0, {}, scrr::params::id::drywet); const auto master = p->getMacroMappings()[1];
    REQUIRE (master.high < master.centre); REQUIRE (std::abs (master.endOffset + .2) < .00001);
    juce::MemoryBlock saved; p->getStateInformation (saved); auto other = testProcessor(); other->setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (other->getModulators()[0].random); REQUIRE (other->getMacroMappings()[0].sourceUid == random);
    auto legacy = p->copyPresetState(); legacy.removeChild (legacy.getChildWithName ("modulators"), nullptr); legacy.removeChild (legacy.getChildWithName ("macroMappings"), nullptr);
    auto old = legacy.getChildWithName ("ch1").getChild (0); old.setProperty ("randomSeed", true, nullptr); old.setProperty ("seedRate", 7.0f, nullptr);
    REQUIRE (other->applyPreset (legacy)); REQUIRE (other->getModulators().size() == 1); REQUIRE (other->getModulators()[0].random);
    REQUIRE (std::abs (other->getModulators()[0].rate - 7) < .0001f); REQUIRE (other->getMacroMappings().size() == 1); REQUIRE (other->getMacroMappings()[0].parameter.toString() == "seed");
    REQUIRE (! other->getModuleChain (1).getChild (0).hasProperty ("randomSeed"));
    for (const auto& spec : scrr::dsp::getModuleSpecs()) for (const auto& parameter : spec.params)
        REQUIRE (parameter.id != "randomSeed" && parameter.id != "seedRate" && (parameter.id != "randomRate" || spec.typeId == "ComplexRotation"));
    const auto once = other->copyPresetState(); REQUIRE (other->applyPreset (once)); REQUIRE (other->getModulators().size() == 1);
}

TEST_CASE ("Sidechain follower supports mono and stereo main buses without sidechain output leakage")
{
    for (bool mono : { false, true }) for (bool sideEnabled : { false, true })
    {
        auto p = testProcessor(); auto layout = p->getBusesLayout();
        layout.inputBuses.set (0, mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, layout.inputBuses[0]);
        layout.inputBuses.set (1, sideEnabled ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        REQUIRE (p->setBusesLayout (layout));
        p->addModulator (true); auto source = p->getModulators()[0]; source.input = 5; source.attack = 1; p->updateModulator (source);
        p->prepareToPlay (48000, 128); const int mainChannels = mono ? 1 : 2;
        juce::AudioBuffer<float> audio (p->getTotalNumInputChannels(), 128); juce::MidiBuffer midi;
        for (int block = 0; block < 100; ++block)
        {
            audio.clear();
            if (sideEnabled) for (int c = mainChannels; c < audio.getNumChannels(); ++c) for (int i = 0; i < 128; ++i) audio.setSample (c, i, .5f);
            p->processBlock (audio, midi);
            for (int c = 0; c < mainChannels; ++c) REQUIRE (audio.getMagnitude (c, 0, 128) < 1.0e-7f);
        }
        REQUIRE (std::abs (p->getModulationValues()[8] - (sideEnabled ? .5f : 0)) < .001f);
        source.input = 0; p->updateModulator (source); p->prepareToPlay (48000, 128);
        audio.clear(); if (sideEnabled) for (int i = 0; i < 128; ++i) audio.setSample (mainChannels, i, 1);
        p->processBlock (audio, midi); REQUIRE (p->getModulationValues()[8] < .0001f);
    }
}

TEST_CASE ("LFO sources modulate audio-owned parameters and disabling a bipolar source removes its contribution")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString(), sourceId = p->addModulator (false);
    auto source = p->getModulators()[0]; source.shape = 2; source.rate = 2; p->updateModulator (source);
    p->assignModulator (sourceId, 1, uid, "shift"); auto m = p->getMacroMappings()[0]; m.setDepth (.1, true); p->updateMacroMapping (m);
    p->prepareToPlay (48000, 128); juce::AudioBuffer<float> input (2, 128); juce::MidiBuffer midi;
    for (int i = 0; i < 48; ++i) { input.clear(); p->processBlock (input, midi); }
    REQUIRE (p->getModulationValues()[8] > .2f); REQUIRE (p->getModulationValues()[8] < .3f);
    REQUIRE (p->getEffectiveValue (1, uid, "shift", 0) < -400);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"]) < .001);
    source.enabled = false; p->updateModulator (source); input.clear(); p->processBlock (input, midi);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "shift", 0)) < .001);
}

TEST_CASE ("Native modulation page adds sources, edits curves, toggles channels and moves copies across CH tabs")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* fftChoice = dynamic_cast<juce::ComboBox*> (findID (*editor, "fft-size")); REQUIRE (fftChoice);
    REQUIRE (fftChoice->getSelectedId() == 1); REQUIRE (fftChoice->getText() == "2048 / L");
    setParam (*p, scrr::params::id::fftSize, 2); pump (100);
    REQUIRE (fftChoice->getSelectedId() == 1); REQUIRE (fftChoice->getText() == "4096 / L");
    findButton (*editor, "MODULATION")->triggerClick(); pump();
    auto* page = findID (*editor, "modulation-page"); REQUIRE (page && page->isVisible());
    findButton (*page, "+ LFO")->triggerClick(); pump(); findButton (*page, "+ ENVELOPE")->triggerClick(); pump();
    REQUIRE (p->getModulators().size() == 2);
    findButton (*page, "+ RANDOM")->triggerClick(); pump(); REQUIRE (p->getModulators().size() == 3); REQUIRE (p->getModulators()[2].random);
    const auto source = p->getModulators()[0]; auto* card = findID (*page, "modulator-" + source.uid); REQUIRE (card);
    scrr::gui::ModulationCurve* curve {};
    for (auto* child : card->getChildren()) if (auto* c = dynamic_cast<scrr::gui::ModulationCurve*> (child)) curve = c;
    REQUIRE (curve); curve->mouseDoubleClick (mouse (*curve, { (float) curve->getWidth() * .4f, (float) curve->getHeight() - 6 }));
    REQUIRE (p->getModulators()[0].shape == 5); REQUIRE (p->getModulators()[0].points.size() >= 9);
    findButton (*page, "EFFECTS >")->triggerClick(); pump(); REQUIRE (! page->isVisible());
    auto* dot = findID (*editor, "channel-2"); dot->mouseDown (mouse (*dot, { 10, 15 })); dot->mouseUp (mouse (*dot, { 10, 15 }));
    REQUIRE (p->getAPVTS().getRawParameterValue (scrr::params::id::channelEnabled[1])->load() < .5f);
    auto* row = findID (*editor, "stage-" + uid); REQUIRE (row);
    const auto point = row->getLocalPoint (dot, juce::Point<int> (40, 15)).toFloat();
   #if JUCE_MAC
    const int modifier = juce::ModifierKeys::commandModifier;
   #else
    const int modifier = juce::ModifierKeys::ctrlModifier;
   #endif
    const int mods = modifier | juce::ModifierKeys::leftButtonModifier;
    row->mouseDown (mouse (*row, { 40, 20 }, false, mods)); row->mouseDrag (mouse (*row, point, true, mods)); row->mouseUp (mouse (*row, point, true, mods)); pump (320);
    REQUIRE (p->getModuleChain (1).getNumChildren() == 1); REQUIRE (p->getModuleChain (2).getNumChildren() == 1);
    REQUIRE (p->getModuleChain (2).getChild (0)["uid"].toString() != uid);
    editor.reset(); pump (100);
}

TEST_CASE ("Invalid GENERIC and FFT drag order shows a notice and preserves the chain")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->addModule (1, "Reverb");
    const auto fft = p->getModuleChain (1).getChild (0)["uid"].toString(), generic = p->getModuleChain (1).getChild (1)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->addToDesktop (0); editor->setVisible (true); pump (320);
    const auto attempt = [&] (const juce::String& source, const juce::String& target, int y)
    {
        auto* row = findID (*editor, "stage-" + source); auto* other = findID (*editor, "stage-" + target); REQUIRE (row && other);
        const auto point = row->getLocalPoint (other, juce::Point<int> (40, y)).toFloat();
        row->mouseDown (mouse (*row, { 40, 20 })); row->mouseDrag (mouse (*row, point, true));
        auto* notice = dynamic_cast<juce::Label*> (findID (*editor, "chain-order-notice")); REQUIRE (notice && notice->isVisible());
        REQUIRE (notice->getText().contains ("GENERIC EFFECTS MUST STAY AT THE END"));
        row->mouseUp (mouse (*row, point, true)); pump (100);
        REQUIRE (p->getModuleChain (1).getChild (0)["uid"].toString() == fft); REQUIRE (p->getModuleChain (1).getChild (1)["uid"].toString() == generic);
        REQUIRE (notice->isVisible());
    };
    attempt (generic, fft, 3); attempt (fft, generic, 52);
    editor.reset(); pump (50);
}

TEST_CASE ("Prompt languages persist separately and localize activation and dialogs only")
{
    using Lang = scrr::gui::PromptLanguage;
    struct Restore { ~Restore() { Lang::set (Lang::Language::english, false); } } restore;
    const auto directory = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile ("language-regression", "");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    const auto file = directory.getChildFile ("language.txt");
    REQUIRE (Lang::read (file) == Lang::Language::english);
    scrr::gui::SettingsPanel settings (file); settings.setSize (520, 414);
    auto* language = dynamic_cast<juce::ComboBox*> (findID (settings, "prompt-language")); REQUIRE (language != nullptr);
    language->setSelectedId (2, juce::sendNotificationSync);
    REQUIRE (Lang::get() == Lang::Language::chinese); REQUIRE (Lang::read (file) == Lang::Language::chinese);
    REQUIRE (findButton (settings, juce::String::fromUTF8 (u8"关闭  x")) != nullptr);
    for (const auto* key : { "Invalid activation code format.", "This activation code belongs to another machine.", "RELEASE TO REMOVE", "GENERIC EFFECTS MUST STAY AT THE END OF THE CHAIN", "Save preset", "Cancel" })
        REQUIRE (scrr::gui::promptText (key) != key);
    REQUIRE (scrr::gui::promptText ("Compressor") == "Compressor");
    scrr::licensing::LicenseManager manager;
    scrr::gui::LicensePanel panel (manager); panel.setSize (620, 452);
    REQUIRE (findButton (panel, juce::String::fromUTF8 (u8"复制")) != nullptr);
    REQUIRE (findButton (panel, juce::String::fromUTF8 (u8"激活")) != nullptr);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        auto p = testProcessor(); auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        editor->addToDesktop (0); editor->setVisible (true); pump (80);
        REQUIRE (findButton (*editor, "MODULATION") != nullptr);
        auto* button = dynamic_cast<juce::Button*> (findID (*editor, "settings-button")); REQUIRE (button != nullptr);
        button->triggerClick(); pump (40); REQUIRE (findID (*editor, "settings-panel")->isVisible());
        findButton (*findID (*editor, "settings-panel"), juce::String::fromUTF8 (u8"关闭  x"))->triggerClick(); pump (40);
        REQUIRE (! findID (*editor, "settings-panel")->isVisible());
        findButton (*editor, "SAVE")->triggerClick(); pump (40);
        auto* dialog = dynamic_cast<juce::AlertWindow*> (juce::Component::getCurrentlyModalComponent()); REQUIRE (dialog != nullptr);
        REQUIRE (dialog->getName() == juce::String::fromUTF8 (u8"保存预设"));
        findButton (*dialog, juce::String::fromUTF8 (u8"取消"))->triggerClick(); pump (60);
        REQUIRE (juce::Component::getNumCurrentlyModalComponents() == 0);
        const auto before = p->copyPresetState().toXmlString();
        Lang::set (Lang::Language::english, false);
        REQUIRE (waitForUI ([&] { return findButton (*findID (*editor, "license-panel"), "ACTIVATE") != nullptr; }));
        REQUIRE (p->copyPresetState().toXmlString() == before);
    }
    else std::cout << "[SKIP native localized dialog subsection: no display]\n";
    language->setSelectedId (1, juce::sendNotificationSync); REQUIRE (Lang::read (file) == Lang::Language::english);
    file.replaceWithText ("unknown"); REQUIRE (Lang::read (file) == Lang::Language::english);
}

TEST_CASE ("Generic card plots respond to parameters and share audio transfer curves")
{
    struct Change { const char* type; const char* parameter; float value; };
    const Change changes[] {
        { "Compressor", "threshold", -40 }, { "Compressor", "ratio", 1 }, { "Compressor", "attack", 200 },
        { "Compressor", "release", 2000 }, { "Compressor", "knee", 24 }, { "Compressor", "makeup", 18 }, { "Compressor", "mix", 0 },
        { "Distortion", "mode", 2 }, { "Distortion", "drive", 30 }, { "Distortion", "bias", .7f },
        { "Distortion", "output", -24 }, { "Distortion", "tone", 500 }, { "Distortion", "mix", 0 },
        { "Reverb", "mode", 1 }, { "Reverb", "size", 0 }, { "Reverb", "decay", 10 },
        { "Delay", "mode", 1 }, { "Delay", "mode", 2 }, { "Delay", "time", 1600 }, { "Delay", "feedback", 90 }
    };
    scrr::gui::EffectCard card; card.setSize (540, 140);
    for (const auto& change : changes)
    {
        auto state = scrr::dsp::createDefaultModuleState (change.type); card.setEffect (change.type, change.type, 1);
        card.setParameterPreview (state);
        const auto bounds = juce::Rectangle<int> (360, 18, 166, 94); // Plot only: no title, caption or animation.
        const auto before = card.createComponentSnapshot (bounds);
        state.setProperty (change.parameter, change.value, nullptr); card.setParameterPreview (state);
        REQUIRE (! samePixels (before, card.createComponentSnapshot (bounds)));
    }
    REQUIRE (std::abs (scrr::dsp::compressorReduction (0, -20, 4, 0) + 15) < 1.0e-6f);
    REQUIRE (std::abs (scrr::dsp::distortionShape (1, 2) - 1) < 1.0e-6f);
    REQUIRE (std::abs (scrr::dsp::distortionShape (2, 3) - 0) < 1.0e-6f);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "Delay"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "time");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->addToDesktop (0); editor->setVisible (true); pump (340);
    auto* live = dynamic_cast<scrr::gui::EffectCard*> (findID (*editor, "effect-card")); REQUIRE (live != nullptr);
    REQUIRE (waitForUI ([&] { return live->getAlpha() >= .999f; }));
    live->animate (0, 1); const auto before = live->createComponentSnapshot (live->getLocalBounds());
    setParam (*p, scrr::params::id::macros[0], 100);
    REQUIRE (waitForUI ([&]
    {
        live->animate (0, 1); // Hold halftone phase fixed: only parameter artwork may change.
        return ! samePixels (before, live->createComponentSnapshot (live->getLocalBounds()));
    }));
    REQUIRE (std::abs ((float) p->getModuleChain (1).getChild (0)["time"] - 300) < 1.0e-6f);
}

TEST_CASE ("Every Hz control uses a reversible perceptual scale with usable low-frequency travel")
{
    int frequencyControls = 0;
    for (const auto& module : scrr::dsp::getModuleSpecs())
        for (const auto& spec : module.params)
        {
            if (spec.type != scrr::dsp::ParamSpec::Float && spec.type != scrr::dsp::ParamSpec::Int) continue;
            int callbacks = 0;
            scrr::gui::ParameterControl control (spec, spec.defaultVal, [&] (const auto&) { ++callbacks; });
            auto* slider = control.getAssignableSlider(); REQUIRE (slider != nullptr); REQUIRE (callbacks == 0);
            if (spec.unit.trim() != "Hz")
            {
                if (! spec.logScale)
                    REQUIRE (std::abs (slider->proportionOfLengthToValue (.5) - (spec.minVal + spec.maxVal) * .5) < .0001);
                continue;
            }
            ++frequencyControls;
            REQUIRE (std::abs (slider->proportionOfLengthToValue (0) - spec.minVal) < .0001);
            REQUIRE (std::abs (slider->proportionOfLengthToValue (1) - spec.maxVal) < .0001);
            double previous = spec.minVal;
            for (int i = 1; i <= 100; ++i)
            {
                const double position = (double) i / 100;
                const auto value = slider->proportionOfLengthToValue (position);
                REQUIRE (std::isfinite (value)); REQUIRE (value > previous);
                REQUIRE (std::abs (slider->valueToProportionOfLength (value) - position) < 1.0e-10);
                previous = value;
            }
            if (spec.minVal > 0)
            {
                // Genuine log mapping, not just a power curve matching one midpoint.
                const double ratio = (double) spec.maxVal / spec.minVal;
                for (const double position : { .25, .5, .75 })
                    REQUIRE (std::abs (slider->proportionOfLengthToValue (position) - spec.minVal * std::pow (ratio, position)) < .0001);
            }
            else
            {
                REQUIRE (std::abs (slider->proportionOfLengthToValue (.5)) < 1.0e-10); // signed Shift
                REQUIRE (std::abs (slider->valueToProportionOfLength (-100) + slider->valueToProportionOfLength (100) - 1) < 1.0e-10);
                REQUIRE (slider->valueToProportionOfLength (100) > .6);
            }
        }
    REQUIRE (frequencyControls >= 10);
    auto processor = testProcessor();
    for (const bool random : { false, true })
    {
        scrr::params::ModulationSource source; source.random = random; source.rate = .37f;
        scrr::gui::ModulatorCard card (*processor, source);
        juce::Slider* rate {};
        for (auto* child : card.getChildren()) if (auto* s = dynamic_cast<juce::Slider*> (child))
            if (s->getTextValueSuffix().trim() == "Hz") rate = s;
        REQUIRE (rate != nullptr);
        REQUIRE (std::abs (rate->proportionOfLengthToValue (.5) - std::sqrt (rate->getMinimum() * rate->getMaximum())) < .0001);
        REQUIRE (std::abs (rate->getValue() - .37) < .0001);
    }
}

TEST_CASE ("Zero-Hz range controls preserve FULL, typed values and existing modulation in the new scale")
{
    for (const double maximum : { 11025, 16000, 22050, 24000, 44100, 48000, 96000 })
    {
        double received = -1;
        scrr::gui::ParameterControl control (scrr::dsp::makeFloat ("rangeHigh", "HIGH / Hz", 0, (float) maximum, (float) maximum, 1, " Hz"),
                                             96000.0, [&] (const auto& v) { received = (double) v; });
        auto* slider = control.getAssignableSlider();
        REQUIRE (received < 0); // Displaying an older FULL value must not rewrite state.
        REQUIRE (std::abs (slider->getValue() - maximum) < .0001);
        REQUIRE (std::abs (slider->proportionOfLengthToValue (0)) < 1.0e-10);
        REQUIRE (std::abs (slider->proportionOfLengthToValue (1) - maximum) < .0001);
        REQUIRE (slider->valueToProportionOfLength (100) > .2);
        REQUIRE (slider->valueToProportionOfLength (1000) > .45);
        REQUIRE (slider->proportionOfLengthToValue (.5) < 1500);
        slider->setValue (slider->getValueFromText ("1234 Hz"), juce::sendNotificationSync);
        REQUIRE (std::abs (received - 1234) < .0001);
        slider->setValue (slider->getValueFromText ("FULL"), juce::sendNotificationSync);
        REQUIRE (std::abs (received - 96000) < .0001); REQUIRE (slider->getTextFromValue (maximum) == "FULL");
    }
    auto p = testProcessor(); p->addModule (1, "SpectralFilter"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "rangeLow", 100.0); p->assignMacro (0, 1, uid, "rangeLow");
    setParam (*p, scrr::params::id::macros[0], 50);
    const auto before = p->getEffectiveValue (1, uid, "rangeLow", 0);
    const auto mappings = p->getMacroMappings();
    scrr::gui::ParameterControl control (scrr::dsp::makeFloat ("rangeLow", "LOW / Hz", 0, 22050, 0, 1, " Hz"), 100.0,
                                        [&] (const auto& v) { p->setModuleParameter (1, uid, "rangeLow", v); });
    control.setSize (440, 61); control.setMacroMappings (mappings); control.refreshMacro (p->getModulationValues());
    auto* slider = control.getAssignableSlider();
    REQUIRE (std::abs (slider->getValue() - 100) < .0001);
    // The range and live marker use the slider's value-to-position conversion too.
    const auto markerValue = mappings[0].value (.5f);
    REQUIRE (std::abs (slider->proportionOfLengthToValue (slider->valueToProportionOfLength (markerValue)) - before) < .0001);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, "rangeLow", 0) - before) < .0001);
    juce::MemoryBlock state; p->getStateInformation (state);
    auto restored = testProcessor(); restored->setStateInformation (state.getData(), (int) state.getSize());
    const auto restoredUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs ((double) restored->getModuleChain (1).getChild (0)["rangeLow"] - 100) < .0001);
    REQUIRE (std::abs (restored->getEffectiveValue (1, restoredUid, "rangeLow", 0) - before) < .0001);
}

TEST_CASE ("Frequency limit saves with state, rescales modulation and preserves stored FULL")
{
    auto p = testProcessor(); REQUIRE (std::abs (p->getFrequencyLimit() - 22050) < .0001);
    p->addModule (1, "SpectralFilter"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["rangeHigh"] - 96000) < .0001);
    p->assignMacro (0, 1, uid, "rangeHigh"); setParam (*p, scrr::params::id::macros[0], 100);
    REQUIRE (std::abs ((p->getEffectiveValue (1, uid, "rangeHigh", 0)) - (17640)) < .0001);
    setParam (*p, scrr::params::id::frequencyLimit, 3);
    REQUIRE (std::abs (p->getFrequencyLimit() - 24000) < .0001);
    REQUIRE (std::abs ((p->getEffectiveValue (1, uid, "rangeHigh", 0)) - (19200)) < .0001);
    REQUIRE (std::abs (p->getMacroMappings()[0].maximum - 24000) < .0001);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["rangeHigh"] - 96000) < .0001);
    juce::MemoryBlock saved; p->getStateInformation (saved);
    auto restored = testProcessor(); restored->setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (std::abs (restored->getFrequencyLimit() - 24000) < .0001);
    const auto restoredUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs ((restored->getEffectiveValue (1, restoredUid, "rangeHigh", 0)) - (19200)) < .0001);
    auto legacy = p->copyPresetState(); legacy.removeChild (legacy.getChildWithProperty ("id", scrr::params::id::frequencyLimit), nullptr);
    REQUIRE (restored->applyPreset (legacy)); REQUIRE (std::abs (restored->getFrequencyLimit() - 22050) < .0001);
}

TEST_CASE ("Spectral processing leaves tones above the selected cap dry, including reversed ranges")
{
    auto render = [] (float limitIndex, bool inverted)
    {
        auto p = testProcessor(); setParam (*p, scrr::params::id::oversample, 0);
        setParam (*p, scrr::params::id::fftSize, 0); setParam (*p, scrr::params::id::frequencyLimit, limitIndex);
        p->addModule (1, "SpectralFilter"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
        p->setModuleParameter (1, uid, "freq", 1000.0);
        if (inverted) { p->setModuleParameter (1, uid, "rangeLow", 2500.0); p->setModuleParameter (1, uid, "rangeHigh", 500.0); }
        p->prepareToPlay (96000, 128); juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
        std::array<std::complex<double>, 2> tones {};
        for (int base = 0; base < 16384; base += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const double t = (double) (base + i) / 96000;
                const float input = (float) (.05 * std::sin (juce::MathConstants<double>::twoPi * 3000 * t)
                                          + .05 * std::sin (juce::MathConstants<double>::twoPi * 30000 * t));
                block.setSample (0, i, input); block.setSample (1, i, input);
            }
            p->processBlock (block, midi);
            if (base >= 4096) for (int i = 0; i < 128; ++i)
                for (int tone = 0; tone < 2; ++tone)
                    tones[(size_t) tone] += (double) block.getSample (0, i) * std::polar (1.0, -juce::MathConstants<double>::twoPi * (tone == 0 ? 3000 : 30000) * (double) (base + i) / 96000);
        }
        return tones;
    };
    for (bool inverted : { false, true })
    {
        const auto limited = render (2, inverted), full = render (6, inverted);
        REQUIRE (std::abs (limited[1]) > 250); // original 30 kHz tone survives the 22.05 kHz cap
        REQUIRE (std::abs (limited[0]) < std::abs (limited[1]) * .02); // 3 kHz is still filtered
        REQUIRE (std::abs (full[1]) < std::abs (limited[1]) * .02); // raising the cap enables processing
    }
}

TEST_CASE ("Signal chain context menu follows a displaced editor instead of the desktop origin")
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay(); if (display == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "SpectralFilter"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    editor->addToDesktop (0); editor->setTopLeftPosition (display->userArea.getPosition() + juce::Point<int> (140, 60));
    editor->setVisible (true); pump (350);
    auto* row = findID (*editor, "stage-" + uid); REQUIRE (row != nullptr);
    const juce::Point<float> click { 70, 20 }; const auto anchor = row->localPointToGlobal (click.toInt());
    row->mouseDown (mouse (*row, click, false, juce::ModifierKeys::rightButtonModifier));
    REQUIRE (waitForUI ([] { return juce::Component::getCurrentlyModalComponent() != nullptr; }));
    auto* menu = juce::Component::getCurrentlyModalComponent();
    REQUIRE (editor->isParentOf (menu));
    const auto bounds = menu->getScreenBounds();
    REQUIRE (std::abs (bounds.getX() - anchor.x) < 32);
    // JUCE flips upwards when the runner's work area has too little room below.
    REQUIRE (juce::jmin (std::abs (bounds.getY() - anchor.y), std::abs (bounds.getBottom() - anchor.y)) < 32);
    juce::PopupMenu::dismissAllActiveMenus(); pump (40);
}

TEST_CASE ("Random Smooth softens changes, preserves runtime on edits and persists with sources")
{
    scrr::params::ModulationSource source; source.uid = "slew"; source.random = source.sync = true;
    source.seed = 42; source.division = 5; source.smooth = 100;
    scrr::dsp::ModulationEngine engine; engine.configure ({ source }); engine.prepare (48000);
    juce::AudioBuffer<float> input (2, 480); input.clear();
    engine.process (input, 0, 480, {}, 120, true, 0, true);
    const float a = source.valueAt (0), b = source.valueAt (1);
    REQUIRE (std::abs ((engine.value (0)) - (a)) < .0001);
    engine.process (input, 0, 480, {}, 120, true, 1, true);
    REQUIRE (std::abs ((engine.value (0)) - (a + (b - a) * (1 - std::exp (-.1)))) < .00001);
    source.name = "Still smooth"; engine.configure ({ source });
    for (int i = 1; i < 10; ++i) engine.process (input, 0, 480, {}, 120, true, 1, true);
    REQUIRE (std::abs ((engine.value (0)) - (a + (b - a) * (1 - std::exp (-1.0)))) < .00001);
    source.retrigger = true; engine.configure ({ source }); engine.process (input, 0, 480, {}, 120, false, 1, true);
    engine.process (input, 0, 480, {}, 120, true, 1, true); REQUIRE (std::abs ((engine.value (0)) - (a)) < .0001);
    source.retrigger = false; source.smooth = 0; engine.configure ({ source });
    engine.process (input, 0, 480, {}, 120, true, 1, true); REQUIRE (std::abs ((engine.value (0)) - (b)) < .0001);
    auto state = source.state(); state.setProperty ("smooth", 1.0e9, nullptr);
    REQUIRE (std::abs (scrr::params::ModulationSource::read (state).smooth - 2000) < .0001);
    state.setProperty ("smooth", std::numeric_limits<double>::infinity(), nullptr);
    REQUIRE (std::abs (scrr::params::ModulationSource::read (state).smooth - 0) < .0001);
    state.removeProperty ("smooth", nullptr); REQUIRE (std::abs (scrr::params::ModulationSource::read (state).smooth - 0) < .0001);
    auto p = testProcessor(); const auto uid = p->addModulator (false, true);
    source = p->getModulators()[0]; source.smooth = 321.5f; p->updateModulator (source);
    juce::MemoryBlock saved; p->getStateInformation (saved);
    auto restored = testProcessor(); restored->setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (restored->getModulators()[0].uid == uid); REQUIRE (std::abs ((restored->getModulators()[0].smooth) - (321.5)) < .0001);
    scrr::gui::ModulatorCard card (*restored, restored->getModulators()[0]); card.setSize (350, 292);
    auto* slider = dynamic_cast<juce::Slider*> (findID (card, "random-smooth")); REQUIRE (slider != nullptr);
    REQUIRE (card.getLocalBounds().contains (slider->getBounds()));
    slider->setValue (50, juce::sendNotificationSync); REQUIRE (std::abs (restored->getModulators()[0].smooth - 50) < .0001);
}

TEST_CASE ("Macro names edit inline and round-trip without changing automation or mapping identity")
{
    auto p = testProcessor(); p->addModule (1, "SpectralContrast"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "amount"); setParam (*p, scrr::params::id::macros[0], 38);
    auto* parameter = p->getAPVTS().getParameter (scrr::params::id::macros[0]); const auto index = parameter->getParameterIndex();
    p->setPresetDirty (false); const auto custom = juce::String::fromUTF8 (u8"空间 / Space"); p->setMacroName (0, "  " + custom + "  ");
    REQUIRE (p->isPresetDirty()); REQUIRE (p->getMacroName (0) == custom);
    REQUIRE (p->getMacroMappings()[0].sourceLabel() == "M1 / " + custom);
    REQUIRE (parameter->getParameterIndex() == index); REQUIRE (std::abs ((p->getMacroValues()[0]) - (.38)) < .0001);
    const auto effective = p->getEffectiveValue (1, uid, "amount", 0);
    juce::MemoryBlock saved; p->getStateInformation (saved);
    auto restored = testProcessor(); restored->setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (restored->getMacroName (0) == custom);
    REQUIRE (std::abs ((restored->getEffectiveValue (1, restored->getModuleChain (1).getChild (0)["uid"].toString(), "amount", 0)) - (effective)) < .0001);
    auto legacy = restored->copyPresetState(); legacy.removeChild (legacy.getChildWithName ("macroNames"), nullptr);
    REQUIRE (restored->applyPreset (legacy)); REQUIRE (restored->getMacroName (0) == "M1");
    restored->setMacroName (0, juce::String::repeatedString ("a", 100)); REQUIRE (restored->getMacroName (0).length() == 32);
    restored->setMacroName (0, "\r\n\t "); REQUIRE (restored->getMacroName (0) == "M1");
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->addToDesktop (0); editor->setVisible (true); pump (100);
    auto* label = dynamic_cast<juce::Label*> (findID (*editor, "macro-name-1")); REQUIRE (label != nullptr); REQUIRE (label->getText() == custom);
    label->showEditor(); auto* field = label->getCurrentTextEditor(); REQUIRE (field != nullptr);
    field->setText ("Motion"); label->hideEditor (false);
    REQUIRE (p->getMacroName (0) == "Motion"); REQUIRE (p->getMacroMappings()[0].sourceLabel() == "M1 / Motion");
    label->showEditor(); label->getCurrentTextEditor()->setText ("Cancelled"); label->hideEditor (true);
    REQUIRE (p->getMacroName (0) == "Motion");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    scrr::gui::PromptLanguage::set (scrr::gui::PromptLanguage::Language::english, false);
    if (argc > 2 && juce::String (argv[1]) == "--validate-vst3")
    {
        juce::AudioPluginFormatManager formats; formats.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (auto* format : formats.getFormats())
            if (format->getName() == "VST3") format->findAllTypesForFile (descriptions, argv[2]);
        if (descriptions.isEmpty()) { std::cerr << "VST3 discovery failed\n"; return 1; }
        std::cout << "Manufacturer: " << descriptions[0]->manufacturerName << '\n';
        if (descriptions[0]->manufacturerName != "dir.oct.") return 1;
        for (int run = 0; run < 30; ++run)
        {
            juce::String error;
            auto instance = formats.createPluginInstance (*descriptions[0], 48000, 128, error);
            if (! instance) { std::cerr << error << '\n'; return 1; }
            int macros = 0;
            for (auto* parameter : instance->getParameters())
                if (parameter->getName (64).startsWith ("Macro "))
                { if (! parameter->isAutomatable()) return 1; ++macros; parameter->setValueNotifyingHost (.37f); }
            if (macros != 8) { std::cerr << "Expected 8 host-automatable macros\n"; return 1; }
            if (instance->getBusCount (true) != 2 || instance->getBus (true, 1)->getName() != "Sidechain")
            { std::cerr << "Expected external Sidechain input bus\n"; return 1; }
            auto layout = instance->getBusesLayout();
            const bool sidechainEnabled = run % 2 == 0;
            layout.inputBuses.set (1, sidechainEnabled ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            if (! instance->setBusesLayout (layout)) { std::cerr << "Sidechain bus configuration failed\n"; return 1; }
            instance->prepareToPlay (48000, 128);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (instance->createEditorIfNeeded());
            juce::AudioBuffer<float> buffer (sidechainEnabled ? 4 : 2, 128); juce::MidiBuffer midi;
            for (int block = 0; block < 40; ++block)
            {
                buffer.clear(); buffer.setSample (0, 0, .01f); instance->processBlock (buffer, midi);
                juce::MemoryBlock saved; instance->getStateInformation (saved);
                if (saved.getSize() == 0) return 1;
                if (block % 10 == 0) instance->setStateInformation (saved.getData(), (int) saved.getSize());
            }
            editor.reset(); instance->releaseResources(); instance.reset();
            pump (12);
        }
        pump (300);
        std::cout << "VST3 host validation: 30 instances, 1200 saves, 120 restores, Sidechain bus toggling and editor teardown passed\n";
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--machine-code")
    { std::cout << scrr::licensing::LicenseManager::machineCode() << std::endl; return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--verify-code")
    {
        const auto result = scrr::licensing::LicenseManager::shared()->validate (juce::File (argv[2]).loadFileAsString());
        std::cout << (result.wasOk() ? "Valid machine activation signature" : result.getErrorMessage()) << std::endl;
        return result.wasOk() ? 0 : 1;
    }
    const bool unlicensedRender = argc > 1 && juce::String (argv[1]) == "--render-unlicensed";
    if (argc > 1 && (juce::String (argv[1]) == "--render" || unlicensedRender))
    {
        auto pStorage = unlicensedRender ? std::make_unique<SpectralCrrptProcessor>() : testProcessor(); auto& p = *pStorage;
        p.addModule (1, "HarmonicMatch"); p.addModule (1, "SpectralContrast"); p.addModule (1, "FrequencyShift");
        auto uid = p.getModuleChain (1).getChild (0)["uid"].toString();
        p.setModuleParameter (1, uid, "shape", 4); p.setModuleParameter (1, uid, "amount", 65.0);
        p.setPresetName ("Harmonic exploration"); p.setPresetDirty (false); p.prepareToPlay (48000, 512);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        editor->addToDesktop (0); editor->setVisible (true);
        juce::AudioBuffer<float> audio (2, 512); juce::MidiBuffer midi;
        for (int frame = 0; frame < 48; ++frame)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
            {
                float value = 0; for (int h = 1; h < 30; ++h) value += .04f / (float) h * std::sin ((float) (frame * 512 + i) * juce::MathConstants<float>::twoPi * 220.0f * (float) h / 48000.0f);
                audio.setSample (c, i, value);
            }
            p.processBlock (audio, midi); pump (45);
        }
        const auto directory = juce::File (argc > 2 ? argv[2] : "/tmp"); directory.createDirectory();
        for (auto size : { juce::Point<int> (1180, 820), juce::Point<int> (1040, 780), juce::Point<int> (1600, 1050) })
        {
            editor->setSize (size.x, size.y); pump (320);
            auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (directory.getChildFile ("spectral-" + juce::String (size.x) + ".png"));
            out.setPosition (0); out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
        editor->setSize (1180, 820); pump (320);
        auto capture = [&] (const juce::String& name)
        {
            auto pixels = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (directory.getChildFile (name + ".png")); out.setPosition (0); out.truncate();
            juce::PNGImageFormat().writeImageToStream (pixels, out);
        };
        if (unlicensedRender)
        {
            capture ("unlicensed");
            dynamic_cast<juce::Button*> (findID (*editor, "license-status"))->triggerClick(); pump (100); capture ("activation-panel");
            return 0;
        }
        p.setMacroName (0, "Motion"); p.setMacroName (1, "Tone");
        p.assignMacro (0, 1, uid, "amount");
        p.assignMacro (0, 1, uid, "moduleMix");
        auto mapping = p.getMacroMappings()[0]; mapping.setDepth (.3, true); p.updateMacroMapping (mapping);
        setParam (p, scrr::params::id::macros[0], 50); pump (100); capture ("macros");
        dynamic_cast<juce::Button*> (findID (*editor, "macro-map-1"))->triggerClick(); pump (100); capture ("macro-mappings");
        findButton (*findID (*editor, "macro-mappings"), "CLOSE  x")->triggerClick(); pump (60);
        setParam (p, scrr::params::id::freqSplitNumSplits, 4); pump (100); capture ("split-channels");
        auto* tab = dynamic_cast<scrr::gui::ChannelTab*> (findButton (*editor, "CH 2"));
        tab->setState (juce::Button::buttonOver); tab->tick (.1f); capture ("channel-hover");
        tab->setState (juce::Button::buttonNormal); tab->tick (.1f);
        findButton (*editor, "ENABLED")->triggerClick(); pump (350); capture ("effect-disabled");
        findButton (*editor, "ENABLED")->triggerClick(); pump (350);
        setParam (p, scrr::params::id::freqSplitCrossfade, 60); pump (100); capture ("split-crossfade");
        setParam (p, scrr::params::id::freqSplitCrossfade, 0); pump (100);
        auto* spectrum = findID (*editor, "input-spectrum");
        spectrum->mouseMove (mouse (*spectrum, { 240, 70 })); capture ("split-readout");
        spectrum->mouseExit (mouse (*spectrum, { -1, -1 }));
        for (const auto* type : { "SpectralJPEG", "SpectralMirror", "SpectralBloom", "SpectralComb", "Reverb", "Delay", "Compressor", "Distortion" })
        {
            p.addModule (1, type); pump (320);
            const auto last = p.getModuleChain (1).getChild (p.getModuleChain (1).getNumChildren() - 1)["uid"].toString();
            auto* row = findID (*editor, "stage-" + last);
            if (row) { row->mouseUp (mouse (*row, { 50, 20 })); pump (340); }
            capture (type);
        }
        const auto lfoId = p.addModulator (false); p.addModulator (true);
        auto source = p.getModulators()[0]; source.shape = 5; source.points[0].curve = .4f; source.points[1].curve = -.3f; p.updateModulator (source);
        auto follower = p.getModulators()[1]; follower.input = 5; p.updateModulator (follower);
        p.assignModulator (lfoId, 1, uid, "amount");
        auto routes = p.getMacroMappings(); auto mod = routes.back(); mod.setDepth (.2, true); p.updateMacroMapping (mod);
        pump (350); findButton (*editor, "MODULATION")->triggerClick(); pump (100); capture ("modulation-page");
        editor->setSize (1040, 780); pump (100); capture ("modulation-minimum"); editor->setSize (1180, 820);
        findButton (*editor, "+ RANDOM")->triggerClick(); pump (100);
        const auto random = p.getModulators().back();
        auto* randomCard = findID (*editor, "modulator-" + random.uid);
        if (randomCard) for (auto* control : randomCard->getChildren()) if (auto* slider = dynamic_cast<juce::Slider*> (control))
        {
            if (std::abs (slider->getMaximum() - 128) < .001) slider->setValue (4, juce::sendNotificationSync);
            if (slider->getComponentID() == "random-smooth") slider->setValue (120, juce::sendNotificationSync);
        }
        for (int frame = 0; frame < 80; ++frame) { audio.clear(); p.processBlock (audio, midi); pump (15); }
        capture ("modulation-random");
        if (randomCard)
        {
            const auto pixels = randomCard->createComponentSnapshot (randomCard->getLocalBounds(), true, 2.0f);
            juce::FileOutputStream out (directory.getChildFile ("random-card.png")); out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (pixels, out);
        }
        setParam (p, scrr::params::id::channelEnabled[0], 0); pump (100); capture ("channel-muted");
        setParam (p, scrr::params::id::channelEnabled[0], 1); pump (100);
        findButton (*findID (*editor, "modulation-page"), "EFFECTS >")->triggerClick(); pump (100);
        findButton (*editor, "+ ADD EFFECT")->triggerClick(); pump (150); capture ("effect-folders");
        findButton (*editor, "CLOSE  x")->triggerClick(); pump (150);
        auto* row = findID (*editor, "stage-" + uid);
        row->mouseUp (mouse (*row, { 50, 20 })); pump (340);
        row = findID (*editor, "stage-" + uid);
        auto* content = editor->getChildComponent (0);
        row->mouseDown (mouse (*row, { 50, 20 }));
        auto target = row->getLocalPoint (content, juce::Point<int> (380, 570)).toFloat();
        row->mouseDrag (mouse (*row, target, true)); capture ("drag-insert");
        target = row->getLocalPoint (content, juce::Point<int> (820, 642)).toFloat();
        row->mouseDrag (mouse (*row, target, true)); capture ("drag-remove");
        row->mouseUp (mouse (*row, { -500, -500 }, true)); pump (100);
        const auto jpegUid = p.getModuleChain (1).getChild (3)["uid"].toString();
        row = findID (*editor, "stage-" + jpegUid); row->mouseUp (mouse (*row, { 50, 20 }));
        for (int frame = 0; frame < 15; ++frame)
        { pump (25); capture ("transition-" + juce::String (frame).paddedLeft ('0', 2)); }
        p.addModule (4, "FrequencyShift"); p.addModule (4, "Delay"); pump (100); findButton (*editor, "CH 4")->triggerClick(); pump (320);
        const auto fourth = p.getModuleChain (4);
        auto* fftRow = findID (*editor, "stage-" + fourth.getChild (0)["uid"].toString());
        auto* genericRow = findID (*editor, "stage-" + fourth.getChild (1)["uid"].toString());
        const auto invalid = genericRow->getLocalPoint (fftRow, juce::Point<int> (40, 3)).toFloat();
        genericRow->mouseDown (mouse (*genericRow, { 40, 20 })); genericRow->mouseDrag (mouse (*genericRow, invalid, true)); capture ("generic-order-warning");
        genericRow->mouseUp (mouse (*genericRow, invalid, true)); pump (100);
        scrr::gui::PromptLanguage::set (scrr::gui::PromptLanguage::Language::chinese, false); pump (100); capture ("generic-order-chinese");
        dynamic_cast<juce::Button*> (findID (*editor, "settings-button"))->triggerClick(); pump (100); capture ("settings-chinese");
        dynamic_cast<juce::Button*> (findID (*editor, "license-status"))->triggerClick(); pump (100); capture ("activation-chinese");
        return 0;
    }
    return doctest::runTests();
}
