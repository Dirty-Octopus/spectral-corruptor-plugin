// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

namespace artworkMotionTests {
inline bool foreground (juce::Colour pixel, juce::Colour background, juce::Colour ink)
{
    const float dr = ink.getFloatRed() - background.getFloatRed(), dg = ink.getFloatGreen() - background.getFloatGreen(), db = ink.getFloatBlue() - background.getFloatBlue();
    const float length = dr * dr + dg * dg + db * db;
    const float projection = (pixel.getFloatRed() - background.getFloatRed()) * dr
        + (pixel.getFloatGreen() - background.getFloatGreen()) * dg + (pixel.getFloatBlue() - background.getFloatBlue()) * db;
    // The halftone is only 18% ink, so it cannot satisfy this mask. This also
    // excludes low-contrast orbit rails, trails and scan lines from the proof.
    return length > .001f && projection / length > .72f;
}
inline int changedForeground (const juce::Image& a, const juce::Image& b, juce::Colour background, juce::Colour ink)
{
    const juce::Image::BitmapData pixelsA (a, juce::Image::BitmapData::readOnly), pixelsB (b, juce::Image::BitmapData::readOnly);
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (foreground (pixelsA.getPixelColour (x, y), background, ink) != foreground (pixelsB.getPixelColour (x, y), background, ink)) ++count;
    return count;
}
inline void writePng (const juce::Image& image, const juce::File& file)
{
    juce::MemoryOutputStream bytes;
    if (! juce::PNGImageFormat().writeImageToStream (image, bytes) || ! file.replaceWithData (bytes.getData(), bytes.getDataSize()))
        throw std::runtime_error ("Motion preview image write failed");
}
}

TEST_CASE ("Every effect header moves its foreground geometry and bypass freezes the entire artwork")
{
    using namespace scrr::gui;
    EffectCard card; card.setSize (540, 140);
    const juce::Rectangle<int> artwork { 350, 10, 180, 115 };
    for (const auto& spec : scrr::dsp::getModuleSpecs())
    {
        if (spec.typeId == "Notes") continue;
        card.setEffect (spec.typeId, spec.displayName, 1, true);
        card.setParameterPreview (scrr::dsp::createDefaultModuleState (spec.typeId)); card.animate (.21f, 1);
        const auto background = effectColour (spec.typeId), ink = background == Theme::blue() ? Theme::white() : Theme::black();
        auto previous = card.createComponentSnapshot (artwork); int changed = 0;
        for (float time : { .64f, 1.07f, 1.50f })
        {
            card.animate (time, 1); const auto next = card.createComponentSnapshot (artwork);
            changed += artworkMotionTests::changedForeground (previous, next, background, ink); previous = next;
        }
        std::cout << "[ARTWORK] " << spec.typeId << " foreground changes: " << changed << '\n';
        REQUIRE (changed >= 75);
        card.setEffect (spec.typeId, spec.displayName, 1, false); card.animate (2, 1);
        const auto frozen = card.createComponentSnapshot (card.getLocalBounds()); card.animate (9, 1);
        REQUIRE (samePixels (frozen, card.createComponentSnapshot (card.getLocalBounds())));
    }
}

TEST_CASE ("Magnitude Phase Swap exchanges distinct circle and square positions each half cycle")
{
    using namespace scrr::gui;
    EffectCard card; card.setSize (540, 140); card.setEffect ("MagnitudePhaseSwap", "Magnitude Phase Swap", 1);
    card.animate (0, 1); const auto before = card.createComponentSnapshot (card.getLocalBounds());
    card.animate (1.4f, 1); const auto swapped = card.createComponentSnapshot (card.getLocalBounds());
    const auto background = effectColour ("MagnitudePhaseSwap"), ink = Theme::white();
    // These two corners lie inside the square but outside the circle. The two
    // identities must exchange sides, not merely wobble or pulse in place.
    const auto squareAt = [&] (const juce::Image& image, int x)
    { return artworkMotionTests::foreground (image.getPixelAt (x, 77), background, ink); };
    REQUIRE (squareAt (before, 390)); REQUIRE (! squareAt (before, 510));
    REQUIRE (! squareAt (swapped, 390)); REQUIRE (squareAt (swapped, 510));
    card.animate (2.8f, 1); const auto looped = card.createComponentSnapshot ({ 350, 10, 180, 115 });
    const auto start = before.getClippedImage ({ 350, 10, 180, 115 });
    REQUIRE (artworkMotionTests::changedForeground (start, looped, background, ink) < 10);
}

TEST_CASE ("Native effect artwork pauses behind pages and while hidden without changing audio state")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "MagnitudePhaseSwap"); const auto state = p->copyPresetState();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 820);
    editor->addToDesktop (0); editor->setVisible (true); editor->toFront (true); pump (360);
    auto* card = dynamic_cast<scrr::gui::EffectCard*> (findID (*editor, "effect-card")); REQUIRE (card);
    const auto snapshot = [&] { return card->createComponentSnapshot (card->getLocalBounds()); };
    auto before = snapshot(); pump (420); REQUIRE (! samePixels (before, snapshot()));
    findButton (*editor, "MODULATION")->triggerClick(); pump (80); before = snapshot(); pump (420);
    REQUIRE (samePixels (before, snapshot()));
    findButton (*editor, "MODULATION")->triggerClick(); pump (80); before = snapshot(); pump (300);
    REQUIRE (! samePixels (before, snapshot()));
    editor->setVisible (false); before = snapshot(); pump (300); REQUIRE (samePixels (before, snapshot()));
    REQUIRE (p->copyPresetState().isEquivalentTo (state));
}

inline int renderEffectMotion (const juce::File& directory)
{
    if (directory.createDirectory().failed() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return 1;
    const char* types[] { "MagnitudePhaseSwap", "ComplexRotation", "PhaseCorruption", "SpectralMirror",
        "FrequencyShift", "FrequencyWarp", "TemporalSmear", "SpectralBloom",
        "SpectralJPEG", "HarmonicSculpt", "SpectralClipper", "SpectralEqualizer",
        "Delay", "Reverb", "Compressor", "Distortion" };
    juce::Component window; window.setSize (2160, 560);
    std::vector<std::unique_ptr<scrr::gui::EffectCard>> cards;
    for (int i = 0; i < 16; ++i)
    {
        auto card = std::make_unique<scrr::gui::EffectCard>();
        const auto* spec = scrr::dsp::findModuleSpec (types[i]); if (! spec) return 1;
        card->setEffect (types[i], spec->displayName, i + 1); auto values = scrr::dsp::createDefaultModuleState (types[i]);
        if (spec->typeId == "SpectralEqualizer") { values.setProperty ("band3Gain", 7, nullptr); values.setProperty ("band5Gain", -9, nullptr); }
        if (spec->typeId == "Delay") { values.setProperty ("time", 900, nullptr); values.setProperty ("feedback", 72, nullptr); }
        card->setParameterPreview (values); window.addAndMakeVisible (*card); card->setBounds ((i % 4) * 540, (i / 4) * 140, 540, 140);
        cards.push_back (std::move (card));
    }
    window.addToDesktop (0); window.setVisible (true); window.toFront (true); pump (100);
    for (int frame = 0; frame < 96; ++frame)
    {
        for (auto& card : cards) card->animate ((float) frame / 24, 1);
        artworkMotionTests::writePng (window.createComponentSnapshot (window.getLocalBounds()), directory.getChildFile ("motion-" + juce::String (frame).paddedLeft ('0', 3) + ".png"));
    }
    // Also capture the live editor clock, rather than only deterministic artwork
    // samples, to verify the production header actually advances on screen.
    auto p = testProcessor(); p->addModule (1, "MagnitudePhaseSwap");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 820);
    editor->addToDesktop (0); editor->setVisible (true); editor->toFront (true); pump (400);
    for (int frame = 0; frame < 48; ++frame)
    {
        pump (42);
        artworkMotionTests::writePng (editor->createComponentSnapshot (editor->getLocalBounds()), directory.getChildFile ("native-swap-" + juce::String (frame).paddedLeft ('0', 3) + ".png"));
    }
    std::cout << "Motion frames: " << directory.getFullPathName() << '\n'; return 0;
}
