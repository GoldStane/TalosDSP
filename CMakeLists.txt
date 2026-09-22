cmake_minimum_required(VERSION 3.20)

project(TalosDSP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TALOSDSP_BUILD_TESTS "Build unit tests" ON)
option(TALOSDSP_BUILD_RTSAFETY "Build the rt-safety symbol check harness" ON)
option(TALOSDSP_SANITIZE "Build with AddressSanitizer + UBSan (offline validation)" OFF)

# PortAudio's CMakeLists declares cmake_minimum_required(2.8), which CMake >= 4
# rejects unless we tell it how far back compatibility is allowed to go.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# PortAudio only calls PROJECT() when it is the top-level project; when pulled
# in via FetchContent the C language never gets enabled, which leaves its
# targets with no linker language. Enable it ourselves.
enable_language(C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TALOSDSP_THREAD_SANITIZE "Build offline concurrency tests with ThreadSanitizer" OFF)
option(TALOSDSP_BUILD_BENCHMARKS "Build offline and loopback benchmarks" OFF)
if(TALOSDSP_SANITIZE AND TALOSDSP_THREAD_SANITIZE)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer need separate builds")
endif()
add_library(talos_options INTERFACE)
target_compile_options(talos_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(TALOSDSP_SANITIZE)
  target_compile_options(talos_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(talos_options INTERFACE -fsanitize=address,undefined)
elseif(TALOSDSP_THREAD_SANITIZE)
  target_compile_options(talos_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(talos_options INTERFACE -fsanitize=thread)
endif()
if((TALOSDSP_SANITIZE OR TALOSDSP_THREAD_SANITIZE) AND TALOSDSP_BUILD_RTSAFETY)
  message(FATAL_ERROR "Disable TALOSDSP_BUILD_RTSAFETY for sanitizer builds")
endif()

# ---- dependencies ----
include(FetchContent)

find_package(PortAudio CONFIG QUIET)
if(TARGET PortAudio::PortAudio)
  set(TALOS_PORTAUDIO PortAudio::PortAudio)
elseif(TARGET portaudio_static)
  set(TALOS_PORTAUDIO portaudio_static)
elseif(TARGET portaudio)
  set(TALOS_PORTAUDIO portaudio)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SYSTEM_PORTAUDIO QUIET IMPORTED_TARGET portaudio-2.0)
  endif()
  if(TARGET PkgConfig::SYSTEM_PORTAUDIO)
    set(TALOS_PORTAUDIO PkgConfig::SYSTEM_PORTAUDIO)
  else()
FetchContent_Declare(
  portaudio
  GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
  GIT_TAG v19.7.0
  GIT_SHALLOW TRUE
)

set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_ASIO OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_WDMKS OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_WASAPI OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_DS OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_WMME OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_WDMKS OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_SKELETON OFF CACHE BOOL "" FORCE)
set(PA_ENABLE_MAC_CORE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(portaudio)

    set(TALOS_PORTAUDIO portaudio_static)
  endif()
endif()

# ---- real-time core: everything reachable from the audio callback ----
add_library(talos_rt OBJECT
  src/audio/AudioCallback.cpp
  src/audio/PassthroughCallback.cpp
  src/audio/XrunCounter.cpp
  src/audio/RtThreadBoost.cpp
  src/audio/StageHistogram.cpp
  src/audio/FreeverbStage.cpp
  src/audio/MixerStage.cpp
  src/audio/LimiterStage.cpp
  src/audio/DspChain.cpp
)
add_library(talos_control STATIC
  src/audio/DspChainControl.cpp
  src/audio/ComplexityController.cpp
  src/audio/Classifier.cpp
  src/audio/Biquad.cpp
  src/audio/Filterbank.cpp
  src/audio/FeatureExtractor.cpp
)
target_include_directories(talos_control PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(talos_control PUBLIC Threads::Threads talos_options)
target_compile_options(talos_rt PRIVATE -fno-exceptions)

set_target_properties(talos_rt PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(talos_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(talos_rt PRIVATE ${TALOS_PORTAUDIO} talos_options)

# ---- rt-safety symbol check (Phase 0 harness) ----
if(TALOSDSP_BUILD_RTSAFETY)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target(rt_safety_check
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/rt-safety/rt_symbol_check.py
            "$<TARGET_OBJECTS:talos_rt>"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "rt-safety: verifying no allocation/locking reachable from the audio callback"
    VERBATIM
  )
  add_dependencies(rt_safety_check talos_rt)

endif()

# ---- application ----
add_executable(talosdsp
  src/main.cpp
  src/audio/AudioEngine.cpp
)
target_include_directories(talosdsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(talosdsp PRIVATE talos_rt talos_control ${TALOS_PORTAUDIO} talos_options)

# ---- tests ----
if(TALOSDSP_BUILD_TESTS)
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(catch2)

  add_executable(talosdsp_tests
    tests/test_xruncounter.cpp
    tests/test_callback.cpp
    tests/test_histogram.cpp
    tests/test_freeverb.cpp
    tests/test_limiter.cpp
    tests/test_mixer.cpp
    tests/test_improvements.cpp
    tests/test_regressions.cpp
    tests/test_chain.cpp
  )
  target_include_directories(talosdsp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(talosdsp_tests PRIVATE talos_rt talos_control Catch2::Catch2WithMain talos_options)

  include(CTest)
  if(BUILD_TESTING)
    include(Catch)
    catch_discover_tests(talosdsp_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME cli_validation
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:talosdsp>)
    add_test(NAME rt_audit_fixtures
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rt_audit.py ${CMAKE_CXX_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/tools/rt-safety/rt_symbol_check.py)
    add_test(NAME rt_symbol_parser
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rt_symbol_check.py)
  endif()
endif()
add_custom_target(check)
if(TALOSDSP_BUILD_RTSAFETY)
  add_dependencies(check rt_safety_check)
endif()
if(TALOSDSP_BUILD_TESTS AND BUILD_TESTING)
  add_custom_target(unit_check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS talosdsp_tests talosdsp
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_dependencies(check unit_check)
endif()

if(TALOSDSP_BUILD_BENCHMARKS)
  add_executable(talos_bench bench/offline.cpp)
  target_include_directories(talos_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(talos_bench PRIVATE talos_rt talos_control talos_options)
  add_executable(loopback_latency bench/loopback_latency.cpp)
  target_include_directories(loopback_latency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(loopback_latency PRIVATE talos_rt talos_control ${TALOS_PORTAUDIO} talos_options)
endif()

if(TALOSDSP_BUILD_BENCHMARKS AND TALOSDSP_BUILD_TESTS AND BUILD_TESTING)
  add_test(NAME loopback_detector COMMAND loopback_latency --self-test)
  add_dependencies(unit_check loopback_latency)
endif()
