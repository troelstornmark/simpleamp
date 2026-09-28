cmake_minimum_required(VERSION 3.22)
project(SimpleAmp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
add_subdirectory(${TP}/JUCE ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# ---- NeuralAmpModelerCore (MIT) ------------------------------------------
set(NAM_DIR ${TP}/NeuralAmpModelerCore)
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
  ${NAM_DIR}
  ${NAM_DIR}/Dependencies/eigen
  ${NAM_DIR}/Dependencies/nlohmann
  ${NAM_DIR}/Dependencies/AudioDSPTools)
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_compile_options(nam_core PRIVATE -O3 -march=haswell -mtune=haswell -w)
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Plugin --------------------------------------------------------------
juce_add_plugin(SimpleAmp
  COMPANY_NAME "Tornmark"
  PLUGIN_MANUFACTURER_CODE Trnm
  PLUGIN_CODE SAmp
  FORMATS AU VST3 Standalone
  PRODUCT_NAME "SimpleAmp"
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
  BUNDLE_ID "com.tornmark.simpleamp"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx Distortion
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "SimpleAmp needs audio input to hear your guitar."
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SimpleAmp PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(SimpleAmp PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_options(SimpleAmp PRIVATE -O3 -march=haswell -mtune=haswell)

target_link_libraries(SimpleAmp
  PRIVATE
    nam_core
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- SimpleAmp Live: standalone app with the tones bundled ----------------
juce_add_gui_app(SimpleAmpLive
  PRODUCT_NAME "SimpleAmp Live"
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
  COMPANY_NAME "Tornmark"
  BUNDLE_ID "com.tornmark.simpleamp.live"
  VERSION 1.0.0
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "SimpleAmp Live needs audio input to hear your guitar.")
target_sources(SimpleAmpLive PRIVATE Source/LiveApp.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(SimpleAmpLive PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="SimpleAmp Live" JUCE_APPLICATION_VERSION_STRING="1.0.0")
target_compile_options(SimpleAmpLive PRIVATE -O3 -march=haswell -mtune=haswell)
target_link_libraries(SimpleAmpLive PRIVATE nam_core juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
# Bundle every .nam and .wav under tones/ (amps: any folder; pedals/, cabs/, reverbs/ are special).
add_custom_command(TARGET SimpleAmpLive POST_BUILD
  COMMAND rsync -a --delete --include=*/ --include=*.nam --include=*.wav --exclude=*
          ${CMAKE_CURRENT_SOURCE_DIR}/tones/ $<TARGET_BUNDLE_CONTENT_DIR:SimpleAmpLive>/Resources/tones/
  VERBATIM)

# ---- Headless test host (not installed) ----------------------------------
juce_add_console_app(SimpleAmpTest PRODUCT_NAME "SimpleAmpTest")
target_sources(SimpleAmpTest PRIVATE tests/HostTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(SimpleAmpTest PRIVATE
  JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=1 SIMPLEAMP_TEST_MODELS="${NAM_DIR}/example_models"
  SIMPLEAMP_TONES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tones")
target_compile_options(SimpleAmpTest PRIVATE -O3 -march=haswell)
target_link_libraries(SimpleAmpTest PRIVATE nam_core juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags)
