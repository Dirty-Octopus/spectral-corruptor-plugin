// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_gui_basics/native/juce_WindowsHooks_windows.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace juce
{

class CustomStandaloneFilterApp final : public JUCEApplication
{
public:
    CustomStandaloneFilterApp()
    {
        PropertiesFile::Options options;
        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "SpectralCorruptor";
        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override           { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override           { return true; }
    void anotherInstanceStarted (const String&) override {}

    StandaloneFilterWindow* createWindow()
    {
        return new StandaloneFilterWindow (getApplicationName(),
                                           LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                                           createPluginHolder());
    }

    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
        return std::make_unique<StandalonePluginHolder> (appProperties.getUserSettings(),
                                                         false, String{}, nullptr,
                                                         Array<StandalonePluginHolder::PluginInOuts>{}, false);
    }

    void initialise (const String&) override
    {
        mainWindow = rawToUniquePtr (createWindow());
        if (mainWindow != nullptr)
            mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
};

} // namespace juce

juce::JUCEApplicationBase* juce_CreateApplication();

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new juce::CustomStandaloneFilterApp();
}

#endif
