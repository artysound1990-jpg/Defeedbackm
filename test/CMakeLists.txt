juce_add_console_app(DeFeedbackTest PRODUCT_NAME "DeFeedbackTest")
target_sources(DeFeedbackTest PRIVATE harness.cpp)
target_compile_features(DeFeedbackTest PRIVATE cxx_std_17)
target_compile_definitions(DeFeedbackTest PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(DeFeedbackTest PRIVATE juce::juce_audio_utils juce::juce_dsp)

# ASAN เฉพาะบน GCC/Clang และใส่เฉพาะไฟล์ของเรา (โมดูล JUCE ทั้งก้อนจะกินแรมจน OOM)
# ASAN ดัก allocator ทั้งโปรเซส จึงยังจับ heap overflow ในโค้ดเราได้ครบ
if(NOT MSVC)
  set_source_files_properties(harness.cpp PROPERTIES
    COMPILE_OPTIONS "-fsanitize=address;-fno-omit-frame-pointer;-g;-O1")
  target_link_options(DeFeedbackTest PRIVATE -fsanitize=address)
endif()

juce_add_console_app(DeFeedbackBench PRODUCT_NAME "DeFeedbackBench")
target_sources(DeFeedbackBench PRIVATE bench.cpp)
target_compile_features(DeFeedbackBench PRIVATE cxx_std_17)
target_compile_definitions(DeFeedbackBench PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(DeFeedbackBench PRIVATE juce::juce_audio_utils juce::juce_dsp)
