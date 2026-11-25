# Mystery Box Synthesizer -- JUCE plugin target (VST3 / AU / Standalone)

set(MBX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MBX_FORMATS AU)
endif()

juce_add_plugin(MysteryBoxSynth
    COMPANY_NAME "Raygun"
    COMPANY_WEBSITE "https://github.com/raygun"
    PLUGIN_MANUFACTURER_CODE Rygn
    PLUGIN_CODE MbxS
    BUNDLE_ID com.raygun.mysteryboxsynth
    PRODUCT_NAME "Mystery Box Synthesizer"
    DESCRIPTION "Win 2,500 instruments from a Mystery Box by surviving a zombie mini-game"
    VERSION ${PROJECT_VERSION}
    FORMATS ${MBX_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE TRUE
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    APP_SANDBOX_ENABLED FALSE)

juce_generate_juce_header(MysteryBoxSynth)

juce_add_binary_data(MbxBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${PROJECT_SOURCE_DIR}/Resources/presets.bin
        ${PROJECT_SOURCE_DIR}/Resources/fonts/BlackOpsOne-Regular.ttf
        ${PROJECT_SOURCE_DIR}/Resources/fonts/CabinSketch-Bold.ttf
        ${PROJECT_SOURCE_DIR}/Resources/fonts/ShareTechMono-Regular.ttf
        ${PROJECT_SOURCE_DIR}/Resources/fonts/Oswald.ttf)
set_target_properties(MbxBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(MysteryBoxSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/game/GameWorld.cpp
    Source/game/GameView.cpp
    Source/ui/Theme.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/TitlePanel.cpp
    Source/ui/MainPanels.cpp
    Source/ui/SidePanels.cpp
    Source/util/PersistentStore.cpp
    Source/util/RealtimeGuard.cpp)

target_include_directories(MysteryBoxSynth PRIVATE Source)

target_compile_definitions(MysteryBoxSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(MysteryBoxSynth
    PRIVATE
        MbxBinaryData
        mbx_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(MysteryBoxSynth PRIVATE /wd4244 /wd4267)
endif()

# Headless test harness that links the plugin code (state round-trip, stress, UI snapshot)
if(MBX_BUILD_TESTS)
    add_executable(mbx_plugin_tests tests/test_plugin.cpp)
    target_include_directories(mbx_plugin_tests PRIVATE Source $<TARGET_PROPERTY:MysteryBoxSynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(mbx_plugin_tests PRIVATE $<TARGET_PROPERTY:MysteryBoxSynth,COMPILE_DEFINITIONS>)
    target_link_libraries(mbx_plugin_tests PRIVATE MysteryBoxSynth MbxBinaryData mbx_engine
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_opengl juce::juce_recommended_config_flags)
    add_test(NAME mbx_plugin_tests COMMAND mbx_plugin_tests)
endif()

# Headless VST3 host smoke test for the built binary (complements pluginval in CI)
if(MBX_BUILD_TOOLS)
    juce_add_console_app(mbx_host_check PRODUCT_NAME "mbx_host_check")
    target_sources(mbx_host_check PRIVATE tools/host_check.cpp)
    juce_generate_juce_header(mbx_host_check)
    target_compile_definitions(mbx_host_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(mbx_host_check PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
