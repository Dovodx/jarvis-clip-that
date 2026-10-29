cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(BUILD_WAV_TEST "Build jarvis-wav-test, a CLI that runs detection on a WAV file" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Third-party downloads ---------------------------------------------------
# Vosk ships prebuilt runtimes, which are bundled with the plugin and loaded at
# runtime; the model is the small (40 MB) English one.

set(VOSK_VERSION 0.3.45)
set(MINIAUDIO_VERSION 0.11.25)
set(VOSK_MODEL vosk-model-small-en-us-0.15)
set(DEPS_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps")

function(fetch_archive url sha256 dest_dir)
  get_filename_component(_archive_name "${url}" NAME)
  set(_archive "${DEPS_DIR}/${_archive_name}")
  if(NOT EXISTS "${dest_dir}")
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${_archive}" EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${DEPS_DIR}")
    file(REMOVE "${_archive}")
  endif()
endfunction()

if(WIN32)
  fetch_archive(
    "https://github.com/alphacep/vosk-api/releases/download/v${VOSK_VERSION}/vosk-win64-${VOSK_VERSION}.zip"
    f1dcc9cca460630f81ea8f71794f69c80bed6556d2a4e6237b5785e1d2dff34b
    "${DEPS_DIR}/vosk-win64-${VOSK_VERSION}"
  )
  set(VOSK_RUNTIME_DIR "${DEPS_DIR}/vosk-win64-${VOSK_VERSION}")
  set(
    VOSK_RUNTIME_FILES
    "${VOSK_RUNTIME_DIR}/libvosk.dll"
    "${VOSK_RUNTIME_DIR}/libgcc_s_seh-1.dll"
    "${VOSK_RUNTIME_DIR}/libstdc++-6.dll"
    "${VOSK_RUNTIME_DIR}/libwinpthread-1.dll"
  )
  set(PLUGIN_DATA_DESTINATION "${CMAKE_PROJECT_NAME}/data")
elseif(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  fetch_archive(
    "https://github.com/alphacep/vosk-api/releases/download/v${VOSK_VERSION}/vosk-linux-x86_64-${VOSK_VERSION}.zip"
    bbdc8ed85c43979f6443142889770ea95cbfbc56cffb5c5dcd73afa875c5fbb2
    "${DEPS_DIR}/vosk-linux-x86_64-${VOSK_VERSION}"
  )
  set(VOSK_RUNTIME_DIR "${DEPS_DIR}/vosk-linux-x86_64-${VOSK_VERSION}")
  set(VOSK_RUNTIME_FILES "${VOSK_RUNTIME_DIR}/libvosk.so")
  set(PLUGIN_DATA_DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}")
else()
  message(FATAL_ERROR "Only Windows x64 and Linux x86_64 are supported (no prebuilt Vosk for this platform)")
endif()

fetch_archive(
  "https://alphacephei.com/vosk/models/${VOSK_MODEL}.zip"
  30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498
  "${DEPS_DIR}/${VOSK_MODEL}"
)

fetch_archive(
  "https://github.com/mackron/miniaudio/archive/refs/tags/${MINIAUDIO_VERSION}.tar.gz"
  b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
  "${DEPS_DIR}/miniaudio-${MINIAUDIO_VERSION}"
)

add_library(miniaudio STATIC "${DEPS_DIR}/miniaudio-${MINIAUDIO_VERSION}/miniaudio.c")
target_include_directories(miniaudio PUBLIC "${DEPS_DIR}/miniaudio-${MINIAUDIO_VERSION}")
target_compile_definitions(
  miniaudio
  PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE
)
set_target_properties(miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(miniaudio PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)
if(UNIX)
  target_link_libraries(miniaudio PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()

# --- Plugin -----------------------------------------------------------------

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

add_library(jarvis-core STATIC src/phrase-detector.cpp src/vosk-runtime.cpp src/voice-listener.cpp)
target_link_libraries(jarvis-core PUBLIC OBS::libobs plugin-support miniaudio)
set_target_properties(jarvis-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE jarvis-core)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Bundle the Vosk runtime and model into the plugin's data directory, both in
# the build's rundir and in the install tree.
install(FILES ${VOSK_RUNTIME_FILES} DESTINATION "${PLUGIN_DATA_DESTINATION}/vosk")
install(DIRECTORY "${DEPS_DIR}/${VOSK_MODEL}/" DESTINATION "${PLUGIN_DATA_DESTINATION}/model")

add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/vosk"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different ${VOSK_RUNTIME_FILES}
    "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/vosk"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_directory "${DEPS_DIR}/${VOSK_MODEL}"
    "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/model"
  COMMENT "Copy Vosk runtime and model to rundir"
  VERBATIM
)

if(BUILD_WAV_TEST)
  add_executable(jarvis-wav-test tools/jarvis-wav-test.cpp)
  target_link_libraries(jarvis-wav-test PRIVATE jarvis-core)
  target_compile_definitions(
    jarvis-wav-test
    PRIVATE
      DEFAULT_VOSK_LIBRARY="${VOSK_RUNTIME_DIR}/$<IF:$<BOOL:${WIN32}>,libvosk.dll,libvosk.so>"
      DEFAULT_MODEL_PATH="${DEPS_DIR}/${VOSK_MODEL}"
  )
endif()
