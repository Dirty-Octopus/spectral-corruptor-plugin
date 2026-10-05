cmake_minimum_required(VERSION 3.22)

project(SpectralCrrpt
    VERSION 0.1.0
    DESCRIPTION "Spectral Corruptor - FFT spectral corruption audio plugin"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/extras/Build/CMake")

option(SCR_BUILD_VST3       "Build VST3 plugin"        ON)
option(SCR_BUILD_AU         "Build AudioUnit plugin"   ON)
option(SCR_BUILD_STANDALONE "Build standalone app"     ON)
option(SCR_COPY_PLUGIN      "Copy plugins to system folders (post-build)" OFF)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
add_subdirectory(third_party/JUCE)

# Resolve formats list (AU only on Apple platforms)
set(SCR_FORMATS VST3)
if(APPLE AND SCR_BUILD_AU)
    list(APPEND SCR_FORMATS AU)
endif()
if(SCR_BUILD_STANDALONE)
    list(APPEND SCR_FORMATS Standalone)
endif()
if(NOT SCR_BUILD_VST3)
    list(REMOVE_ITEM SCR_FORMATS VST3)
endif()

juce_add_plugin(SpectralCrrpt
    PRODUCT_NAME              "Spectral Corruptor Source"
    COMPANY_NAME              "dir.oct."
    BUNDLE_ID                 "com.dirtyoctopus.spectralcorruptor.source"
    PLUGIN_MANUFACTURER_CODE  Scrr
    PLUGIN_CODE               Scos
    FORMATS                   ${SCR_FORMATS}
    VST3_CATEGORIES           Fx Distortion
    AU_MAIN_TYPE              kAudioUnitType_Effect
    AU_EXPORT_PREFIX          SCRROS
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${SCR_COPY_PLUGIN}

    LANGUAGES C CXX
)

target_sources(SpectralCrrpt PRIVATE
    Source/PluginProcessor.cpp
    Source/MacroRouting.cpp
    Source/PluginEditor.cpp
    Source/licensing/LicenseManager.cpp
    Source/StandaloneApp.cpp
    Source/parameters/ParameterLayout.cpp
    Source/dsp/FFTEngine.cpp
    Source/dsp/Oversampler.cpp
    Source/dsp/DryWetMixer.cpp
    Source/dsp/GainStage.cpp
    Source/dsp/SpectralProcessor.cpp
    Source/modules/SpectralModule.cpp
    Source/gui/AssetCache.cpp
    Source/gui/Skin.cpp
    Source/gui/NotesPanel.cpp
    third_party/md4c/md4c.c
    Source/gui/EditorContent.cpp
    Source/gui/PresetBar.cpp
    Source/gui/components/SpectralKnob.cpp
    Source/gui/components/SpectralButton.cpp
    Source/gui/components/SpectralToggle.cpp
    Source/gui/components/SpectrumDisplay.cpp
)

# Keep the unmodified third-party C parser out of JUCE's C++ warning policy.
if(MSVC)
    set_source_files_properties(third_party/md4c/md4c.c PROPERTIES COMPILE_OPTIONS /w)
else()
    set_source_files_properties(third_party/md4c/md4c.c PROPERTIES COMPILE_OPTIONS -w)
endif()

if(MSVC)
    # Prompt translations and their regression fixtures contain UTF-8 literals.
    target_compile_options(SpectralCrrpt PUBLIC /utf-8)
endif()

target_include_directories(SpectralCrrpt PRIVATE
    Source
    Source/gui/components
)

target_compile_definitions(SpectralCrrpt PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

if(APPLE)
    target_compile_definitions(SpectralCrrpt PRIVATE
        JUCE_USE_COREIMAGE_LOADER=1
    )
endif()

target_link_libraries(SpectralCrrpt PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)


# Generate FactoryPresets.h from presets/ folder (supports subfolders)
file(GLOB_RECURSE FACTORY_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.scpreset")
set(FACTORY_PRESETS_H "${CMAKE_CURRENT_SOURCE_DIR}/Source/FactoryPresets.h")
set(_fp_code "#pragma once\n#include <juce_core/juce_core.h>\n\nnamespace scrr::factoryPresets {\n\nstruct Entry {\n    const char* path;\n    const char* folder;\n    const char* name;\n    const char* data;\n};\n\nstatic inline juce::Array<Entry>& getEntries()\n{\n    static auto cachedEntries = [] {\n    juce::Array<Entry> entries;\n")
foreach(PRESET_FILE ${FACTORY_PRESET_FILES})
    file(READ "${PRESET_FILE}" _content)
    file(RELATIVE_PATH _relpath "${CMAKE_CURRENT_SOURCE_DIR}/presets" "${PRESET_FILE}")
    get_filename_component(_name "${PRESET_FILE}" NAME_WE)
    get_filename_component(_folder "${_relpath}" DIRECTORY)
    string(APPEND _fp_code "    entries.add({\"${_relpath}\", \"${_folder}\", \"${_name}\", R\"PRESETDATA(${_content})PRESETDATA\"});\n")
endforeach()
string(APPEND _fp_code "    return entries;\n    }();\n    return cachedEntries;\n}\n\n} // namespace scrr::factoryPresets\n")
file(WRITE "${FACTORY_PRESETS_H}" "${_fp_code}")

add_executable(SpectralCrrptTests
    tests/DspFoundationTests.cpp
    Source/dsp/GainStage.cpp
    Source/dsp/DryWetMixer.cpp
    Source/dsp/FFTEngine.cpp
    Source/dsp/Oversampler.cpp
)

target_include_directories(SpectralCrrptTests PRIVATE
    Source
    tests
)

if(NOT MSVC)
    target_compile_options(SpectralCrrptTests PRIVATE -Wno-error)
endif()

target_link_libraries(SpectralCrrptTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

add_test(NAME SpectralCrrptTests COMMAND SpectralCrrptTests)

include(cmake/PublicTests.cmake)
