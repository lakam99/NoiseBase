cmake_minimum_required(VERSION 3.22)
project(NoiseBase VERSION 1.10.4 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/JUCE)
juce_add_binary_data(NoiseBaseModel SOURCES resources/silero_vad.onnx
    resources/generated/ivory.png resources/generated/graphite.png resources/generated/walnut.png
    resources/generated/knob.png resources/generated/meter.png resources/generated/switches.png
    resources/generated/buttons.png resources/generated/lamps.png
    resources/generated/vfd.png resources/generated/transport.png resources/generated/app-icon.png resources/tests/reference-speech.wav)
juce_add_plugin(NoiseBase
    COMPANY_NAME "RedWeb" BUNDLE_ID "studio.redweb.noisebase"
    PLUGIN_MANUFACTURER_CODE Rdwb PLUGIN_CODE Nbas
    FORMATS VST3 PRODUCT_NAME "NoiseBase"
    ICON_BIG resources/generated/app-icon.png ICON_SMALL resources/generated/app-icon.png
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Restoration
    NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE)
target_sources(NoiseBase PRIVATE
    Source/DenoiseEngine.cpp Source/BackgroundArtifactTests.cpp Source/RustleSuppressor.cpp Source/RustleTests.cpp Source/VoiceDetector.cpp Source/AsyncCleanup.cpp Source/Offline.cpp Source/NoiseOutputTests.cpp Source/ReductionMeterTests.cpp Source/ProceduralNoise.cpp Source/ProceduralTests.cpp Source/NoiseTimeline.cpp Source/TrackTests.cpp Source/HostSafetyTests.cpp
    Source/ReferenceLearner.cpp Source/ReferenceTests.cpp Source/ProfileCache.cpp Source/CacheTests.cpp Source/Processor.cpp Source/Editor.cpp Source/RasterAssets.cpp Source/VfdDisplay.cpp Source/PluginEntry.cpp)
target_include_directories(NoiseBase PUBLIC Source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/modules")
target_include_directories(NoiseBase PRIVATE third_party/onnxruntime/build/native/include)
target_compile_definitions(NoiseBase PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(NoiseBase PRIVATE NoiseBaseModel juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
juce_add_gui_app(NoiseBaseStandalone COMPANY_NAME "RedWeb" PRODUCT_NAME "NoiseBase"
    ICON_BIG resources/generated/app-icon.png ICON_SMALL resources/generated/app-icon.png NEEDS_CURL FALSE NEEDS_WEB_BROWSER FALSE)
target_sources(NoiseBaseStandalone PRIVATE Source/Standalone.cpp Source/OsTheme.cpp)
if(APPLE)
    target_sources(NoiseBaseStandalone PRIVATE Source/UpdaterMac.cpp Source/MacCapture.mm)
else()
    target_sources(NoiseBaseStandalone PRIVATE Source/Updater.cpp)
endif()
target_compile_definitions(NoiseBaseStandalone PRIVATE NOISEBASE_VERSION="${PROJECT_VERSION}")
target_link_libraries(NoiseBaseStandalone PRIVATE NoiseBase juce::juce_recommended_config_flags)
if(WIN32)
    target_link_libraries(NoiseBaseStandalone PRIVATE dwmapi shell32)
    juce_add_gui_app(NoiseBaseTeardownHost COMPANY_NAME "NoiseBase tests" PRODUCT_NAME "NoiseBaseTeardownHost")
    target_sources(NoiseBaseTeardownHost PRIVATE Source/TeardownHost.cpp)
    target_link_libraries(NoiseBaseTeardownHost PRIVATE NoiseBase juce::juce_recommended_config_flags dwmapi)
endif()
target_include_directories(NoiseBaseStandalone PRIVATE third_party/onnxruntime/build/native/include)
if(WIN32)
set(ORT_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/runtimes/win-x64/native/onnxruntime.dll")
set(ORT_SHARED_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/runtimes/win-x64/native/onnxruntime_providers_shared.dll")
foreach(target NoiseBase_VST3 NoiseBaseStandalone)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_SHARED_DLL}" "$<TARGET_FILE_DIR:${target}>")
endforeach()
elseif(APPLE)
    foreach(target NoiseBase_VST3 NoiseBaseStandalone)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/runtimes/osx-arm64/native/libonnxruntime.dylib"
                "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks/libonnxruntime.dylib"
            COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${target}>")
    endforeach()
endif()
if(WIN32)
foreach(script NoiseBase-Update.ps1 Install-NoiseBase.ps1 Release-Common.ps1 Uninstall-NoiseBase.ps1)
    set_property(TARGET NoiseBaseStandalone APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}")
    add_custom_command(TARGET NoiseBaseStandalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}" "$<TARGET_FILE_DIR:NoiseBaseStandalone>/${script}")
endforeach()
endif()
