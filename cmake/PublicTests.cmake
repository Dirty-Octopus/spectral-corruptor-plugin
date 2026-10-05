# The supplied backend must remain locked. Disable this test only after replacing it.
option(SCR_TEST_LICENSE_PLACEHOLDER "Test the supplied unimplemented licensing backend" ON)
option(SCR_BUILD_PROCESSOR_TESTS "Run full processor/UI tests with your own working licensing backend" OFF)

if(SCR_TEST_LICENSE_PLACEHOLDER)
    add_executable(SpectralCrrptPublicBuildTests tests/PublicBuildTests.cpp)
    target_include_directories(SpectralCrrptPublicBuildTests PRIVATE Source tests)
    target_link_libraries(SpectralCrrptPublicBuildTests PRIVATE SpectralCrrpt
        juce::juce_audio_utils juce::juce_dsp)
    add_test(NAME SpectralCrrptPublicBuildTests COMMAND SpectralCrrptPublicBuildTests)
endif()

if(SCR_BUILD_PROCESSOR_TESTS)
    add_executable(SpectralCrrptRegressionTests tests/PluginRegressionTests.cpp)
    target_include_directories(SpectralCrrptRegressionTests PRIVATE Source tests)
    target_link_libraries(SpectralCrrptRegressionTests PRIVATE SpectralCrrpt
        juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(SpectralCrrptRegressionTests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    add_test(NAME SpectralCrrptRegressionTests COMMAND SpectralCrrptRegressionTests)
endif()
