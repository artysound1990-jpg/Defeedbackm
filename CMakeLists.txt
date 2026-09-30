cmake_minimum_required(VERSION 3.22)
project(DeFeedback VERSION 0.3.0)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DeFeedback
  COMPANY_NAME "Arty"
  PLUGIN_MANUFACTURER_CODE Arty
  PLUGIN_CODE DfFb
  FORMATS VST3 Standalone
  PRODUCT_NAME "DeFeedback"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  VST3_CATEGORIES Fx EQ
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DeFeedback PRIVATE Source/DeFeedback.cpp)
target_compile_features(DeFeedback PRIVATE cxx_std_17)
target_compile_definitions(DeFeedback PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DeFeedback
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(DEFEEDBACK_BUILD_TESTS)
  add_subdirectory(test)
endif()

# ASIO ให้ latency ต่ำกว่า WASAPI มาก แต่ SDK ของ Steinberg แจกต่อไม่ได้
# ถ้าจะใช้: โหลด SDK เองแล้วบิ้วด้วย
#   cmake -B build -DASIO_SDK_DIR=C:/path/to/asiosdk
if(ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
  message(STATUS "DeFeedback: เปิดใช้ ASIO จาก ${ASIO_SDK_DIR}")
  target_compile_definitions(DeFeedback PUBLIC JUCE_ASIO=1)
  target_include_directories(DeFeedback PUBLIC "${ASIO_SDK_DIR}/common")
else()
  message(STATUS "DeFeedback: ไม่มี ASIO SDK - standalone จะใช้ WASAPI/DirectSound")
endif()
