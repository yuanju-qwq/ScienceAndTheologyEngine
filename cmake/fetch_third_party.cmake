# Fetch third-party libraries from pre-downloaded archives.
# Run snt_engine/cmake/download_third_party.ps1 BEFORE cmake configure.
# This avoids cmake's curl/schannel issues with GitHub redirects.

include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "")
set(_SNT_DOWNLOADS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/_downloads")

# ============================================================
# Vulkan-Headers (header + loader API)
# ============================================================
# Use pre-downloaded zip to avoid git clone issues.
FetchContent_Declare(
    VulkanHeaders
    URL ${_SNT_DOWNLOADS_DIR}/VulkanHeaders-v1.3.295.zip
)
FetchContent_MakeAvailable(VulkanHeaders)
# Vulkan-Headers' own CMake defines Vulkan::Headers target.

# ============================================================
# Volk — Vulkan loader (dynamic, no SDK required at runtime).
# Loads vulkan-1.dll via LoadLibrary; no need to link vulkan-1.lib.
# ============================================================
FetchContent_Declare(
    Volk
    URL ${_SNT_DOWNLOADS_DIR}/volk-master.zip
)
FetchContent_MakeAvailable(Volk)

# Volk's CMake defines `volk` target (static lib) when VULKAN_HPP_CMAKE is
# OFF (default). It needs Vulkan headers; Vulkan-Headers is fetched above
# so `Vulkan::Headers` target exists.
if(TARGET volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    # Define VK_NO_PROTOTYPES so Volk provides the function pointers.
    target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
endif()

# ============================================================
# Vulkan Memory Allocator (VMA) — header-only
# ============================================================
FetchContent_Declare(
    VulkanMemoryAllocator
    URL ${_SNT_DOWNLOADS_DIR}/VulkanMemoryAllocator-v3.1.0.zip
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# VMA is header-only; expose a thin interface target with include path
# and our config defines (recording + stats for GPU memory analysis).
add_library(vma_config INTERFACE)
target_include_directories(vma_config INTERFACE ${VulkanMemoryAllocator_SOURCE_DIR}/include)
target_compile_definitions(vma_config INTERFACE
    VMA_RECORDING_ENABLED=1
    VMA_STATS_STRING_ENABLED=1
)

# ============================================================
# EnTT (ECS, header-only)
# ============================================================
FetchContent_Declare(
    EnTT
    URL ${_SNT_DOWNLOADS_DIR}/EnTT-v3.13.2.zip
)
FetchContent_MakeAvailable(EnTT)
# EnTT's CMake defines EnTT::EnTT target.

# ============================================================
# stb (image loading, header-only)
# ============================================================
FetchContent_Declare(
    stb
    URL ${_SNT_DOWNLOADS_DIR}/stb-master.zip
)
FetchContent_MakeAvailable(stb)
# stb has no CMake target; create one.
if(NOT TARGET stb::stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
    add_library(stb::stb ALIAS stb)
endif()

# ============================================================
# nlohmann_json (JSON parsing, header-only)
# ============================================================
FetchContent_Declare(
    nlohmann_json
    URL ${_SNT_DOWNLOADS_DIR}/nlohmann_json-v3.11.3.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================
# GLM (math library, header-only)
# ============================================================
# Used for MVP matrices (mat4, vec3, perspective, lookAt).
# Pulled via FetchContent; headers live in _deps/glm-src/glm/.
# To modify GLM later: copy _deps/glm-src/glm/ to engine/core/math/glm/
# and switch the include path below.
FetchContent_Declare(
    GLM
    URL ${_SNT_DOWNLOADS_DIR}/glm-1.0.1.zip
)
FetchContent_MakeAvailable(GLM)
# GLM's CMake defines glm::glm target.

# ============================================================
# tinyobjloader (.obj mesh loading, header-only)
# ============================================================
FetchContent_Declare(
    tinyobjloader
    URL ${_SNT_DOWNLOADS_DIR}/tinyobjloader-release.zip
)
FetchContent_MakeAvailable(tinyobjloader)
# tinyobjloader's CMake defines a `tinyobjloader` target (no namespace).
# The `tinyobjloader::` namespace is only applied on install/export, which
# does not happen in a FetchContent build. Create an ALIAS so downstream
# code can use the `tinyobjloader::tinyobjloader` name consistently
# (matches the pattern used for stb::stb above).
if(NOT TARGET tinyobjloader::tinyobjloader)
    add_library(tinyobjloader::tinyobjloader ALIAS tinyobjloader)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# ============================================================
# Unicode image codecs for FreeType colour glyphs
# ============================================================
# CBDT/sbix colour emoji glyphs are PNG-compressed. These source snapshots
# are vendored with the project so P6 never silently loses emoji rendering
# because a developer machine happens to have libpng installed.
set(_SNT_ZLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
set(_SNT_LIBPNG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng)
if(NOT EXISTS ${_SNT_ZLIB_DIR}/zlib.h OR NOT EXISTS ${_SNT_LIBPNG_DIR}/png.h)
    message(FATAL_ERROR "P6 Unicode text requires vendored zlib and libpng sources")
endif()

add_library(snt_zlib STATIC
    ${_SNT_ZLIB_DIR}/adler32.c
    ${_SNT_ZLIB_DIR}/compress.c
    ${_SNT_ZLIB_DIR}/crc32.c
    ${_SNT_ZLIB_DIR}/deflate.c
    ${_SNT_ZLIB_DIR}/inffast.c
    ${_SNT_ZLIB_DIR}/inflate.c
    ${_SNT_ZLIB_DIR}/inftrees.c
    ${_SNT_ZLIB_DIR}/trees.c
    ${_SNT_ZLIB_DIR}/uncompr.c
    ${_SNT_ZLIB_DIR}/zutil.c
)
target_include_directories(snt_zlib PUBLIC ${_SNT_ZLIB_DIR})
if(MSVC)
    target_compile_options(snt_zlib PRIVATE $<$<CONFIG:Debug>:/MDd> $<$<NOT:$<CONFIG:Debug>>:/MD>)
endif()
add_library(ZLIB::ZLIB ALIAS snt_zlib)

file(GLOB _SNT_LIBPNG_SOURCES CONFIGURE_DEPENDS ${_SNT_LIBPNG_DIR}/*.c)
add_library(snt_libpng STATIC ${_SNT_LIBPNG_SOURCES})
target_include_directories(snt_libpng PUBLIC ${_SNT_LIBPNG_DIR})
target_compile_definitions(snt_libpng PRIVATE PNG_STATIC)
if(MSVC)
    target_compile_options(snt_libpng PRIVATE $<$<CONFIG:Debug>:/MDd> $<$<NOT:$<CONFIG:Debug>>:/MD>)
endif()
target_link_libraries(snt_libpng PUBLIC ZLIB::ZLIB)
add_library(PNG::PNG ALIAS snt_libpng)

# ============================================================
# FreeType (font rasterization)
# ============================================================
set(_FREETYPE_ARCHIVE ${_SNT_DOWNLOADS_DIR}/freetype-VER-2-14-3.zip)
if(NOT EXISTS ${_FREETYPE_ARCHIVE})
    message(FATAL_ERROR "FreeType archive missing: ${_FREETYPE_ARCHIVE}. Run download_third_party.ps1.")
endif()
set(SKIP_INSTALL_ALL ON CACHE BOOL "Disable third-party install exports in the engine build" FORCE)
set(FT_REQUIRE_PNG ON CACHE BOOL "Require libpng for colour emoji" FORCE)
set(FT_REQUIRE_ZLIB ON CACHE BOOL "Require zlib for colour emoji" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable FreeType bzip2 support" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "Disable FreeType brotli support" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable FreeType harfbuzz support" FORCE)
set(FT_DISABLE_PNG OFF CACHE BOOL "Enable FreeType PNG colour glyph support" FORCE)
set(FT_DISABLE_ZLIB OFF CACHE BOOL "Enable FreeType zlib support" FORCE)
FetchContent_Declare(
    FreeType
    URL ${_FREETYPE_ARCHIVE}
)
FetchContent_MakeAvailable(FreeType)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
if(NOT TARGET Freetype::Freetype)
    message(FATAL_ERROR "FreeType target missing after FetchContent_MakeAvailable(FreeType).")
endif()

# Full retained-MUI text stack: ICU4C + HarfBuzz + FreeType.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/text_dependencies.cmake)

# ============================================================
# shaderc (runtime/build-time GLSL -> SPIR-V compilation)
# ============================================================
# Use Vulkan SDK's shaderc package. The standalone shaderc source archive
# requires glslang/SPIRV-Tools sources beside it; the SDK ships a complete
# combined library and headers.
find_path(SHADERC_INCLUDE_DIR
    NAMES shaderc/shaderc.h
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "D:/vulkansdk/Include"
)
find_library(SHADERC_SHARED_IMPLIB_RELEASE
    NAMES shaderc_shared
    HINTS
        "$ENV{VULKAN_SDK}/Lib"
        "D:/vulkansdk/Lib"
)
find_library(SHADERC_SHARED_IMPLIB_DEBUG
    NAMES shaderc_shared
    HINTS
        "$ENV{VULKAN_SDK}/Lib"
        "D:/vulkansdk/Lib"
)
find_file(SHADERC_SHARED_DLL_RELEASE
    NAMES shaderc_shared.dll
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "D:/vulkansdk/Bin"
)
find_file(SHADERC_SHARED_DLL_DEBUG
    NAMES shaderc_shared.dll
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "D:/vulkansdk/Bin"
)
set(SHADERC_SHARED_IMPLIB_DEBUG "${SHADERC_SHARED_IMPLIB_RELEASE}")
set(SHADERC_SHARED_DLL_DEBUG "${SHADERC_SHARED_DLL_RELEASE}")
if(NOT SHADERC_INCLUDE_DIR OR NOT SHADERC_SHARED_IMPLIB_RELEASE OR NOT SHADERC_SHARED_DLL_RELEASE)
    message(FATAL_ERROR "shaderc shared library not found in Vulkan SDK. Set VULKAN_SDK to a SDK with Include/shaderc, Lib/shaderc_shared.lib, and Bin/shaderc_shared.dll.")
endif()
if(NOT TARGET shaderc::shaderc_shared)
    add_library(shaderc::shaderc_shared SHARED IMPORTED)
    set_target_properties(shaderc::shaderc_shared PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SHADERC_INCLUDE_DIR}"
        IMPORTED_IMPLIB_RELEASE "${SHADERC_SHARED_IMPLIB_RELEASE}"
        IMPORTED_IMPLIB_RELWITHDEBINFO "${SHADERC_SHARED_IMPLIB_RELEASE}"
        IMPORTED_IMPLIB_MINSIZEREL "${SHADERC_SHARED_IMPLIB_RELEASE}"
        IMPORTED_IMPLIB_DEBUG "${SHADERC_SHARED_IMPLIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${SHADERC_SHARED_DLL_RELEASE}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${SHADERC_SHARED_DLL_RELEASE}"
        IMPORTED_LOCATION_MINSIZEREL "${SHADERC_SHARED_DLL_RELEASE}"
        IMPORTED_LOCATION_DEBUG "${SHADERC_SHARED_DLL_DEBUG}"
    )
endif()

# ============================================================
# SDL3 (extracted source under third_party/)
# ============================================================
# SDL3 is built from source for full control and debug symbols.
# Source is downloaded by download_third_party.ps1 and extracted to
# third_party/SDL-release-3.4.10/.
set(_SDL3_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL-release-3.4.10)
if(EXISTS ${_SDL3_SOURCE_DIR}/CMakeLists.txt)
    set(SDL_SHARED ON CACHE BOOL "Build SDL as shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Don't build SDL static" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Don't build SDL test lib" FORCE)
    add_subdirectory(${_SDL3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL3-build EXCLUDE_FROM_ALL)
endif()

# ============================================================
# AngelScript (gameplay scripting language, C++ syntax, JIT)
# ============================================================
# Source is downloaded by download_third_party.ps1 and extracted to
# third_party/_downloads/angelscript_2.37.0.zip. The SDK ships its own
# CMake project at angelscript_2.37.0/sdk/angelscript/projects/cmake/,
# but it has no root CMakeLists.txt, so FetchContent_MakeAvailable only
# downloads the source — we compile it ourselves below.
FetchContent_Declare(
    AngelScript
    URL ${_SNT_DOWNLOADS_DIR}/angelscript_2.37.0.zip
)
FetchContent_MakeAvailable(AngelScript)

FetchContent_GetProperties(AngelScript)
set(_AS_SRC "${FETCHCONTENT_BASE_DIR}/angelscript-src")

# Core AS sources — exclude the platform-specific callfunc files we do
# not need on this target (we only use the MSVC x64 path on Windows).
set(_AS_SOURCES
    ${_AS_SRC}/angelscript/source/as_atomic.cpp
    ${_AS_SRC}/angelscript/source/as_builder.cpp
    ${_AS_SRC}/angelscript/source/as_bytecode.cpp
    ${_AS_SRC}/angelscript/source/as_callfunc.cpp
    ${_AS_SRC}/angelscript/source/as_callfunc_x64_msvc.cpp
    ${_AS_SRC}/angelscript/source/as_compiler.cpp
    ${_AS_SRC}/angelscript/source/as_configgroup.cpp
    ${_AS_SRC}/angelscript/source/as_context.cpp
    ${_AS_SRC}/angelscript/source/as_datatype.cpp
    ${_AS_SRC}/angelscript/source/as_gc.cpp
    ${_AS_SRC}/angelscript/source/as_generic.cpp
    ${_AS_SRC}/angelscript/source/as_globalproperty.cpp
    ${_AS_SRC}/angelscript/source/as_memory.cpp
    ${_AS_SRC}/angelscript/source/as_module.cpp
    ${_AS_SRC}/angelscript/source/as_objecttype.cpp
    ${_AS_SRC}/angelscript/source/as_outputbuffer.cpp
    ${_AS_SRC}/angelscript/source/as_parser.cpp
    ${_AS_SRC}/angelscript/source/as_restore.cpp
    ${_AS_SRC}/angelscript/source/as_scriptcode.cpp
    ${_AS_SRC}/angelscript/source/as_scriptengine.cpp
    ${_AS_SRC}/angelscript/source/as_scriptfunction.cpp
    ${_AS_SRC}/angelscript/source/as_scriptnode.cpp
    ${_AS_SRC}/angelscript/source/as_scriptobject.cpp
    ${_AS_SRC}/angelscript/source/as_string.cpp
    ${_AS_SRC}/angelscript/source/as_string_util.cpp
    ${_AS_SRC}/angelscript/source/as_thread.cpp
    ${_AS_SRC}/angelscript/source/as_tokenizer.cpp
    ${_AS_SRC}/angelscript/source/as_typeinfo.cpp
    ${_AS_SRC}/angelscript/source/as_variablescope.cpp
)

# MSVC x64 requires the MASM asm file for native calling conventions
# (CallX64 / GetReturnedFloat / GetReturnedDouble are implemented in
# as_callfunc_x64_msvc_asm.asm; as_callfunc_x64_msvc.cpp references them
# via extern "C"). We enable ASM_MASM and add the .asm directly to the
# angelscript target's source list — CMake will invoke ml64.exe with only
# MASM-recognised flags. Because the engine's /permissive- /Zc:__cplusplus
# /EHsc flags now live on `snt_engine_settings` (an INTERFACE library the
# `angelscript` target never links to), no C++ flags leak into the MASM
# step. Previously these were pushed globally via add_compile_options and
# ml64.exe rejected them, causing the .obj to never land (LNK1181).
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_MASM)
    list(APPEND _AS_SOURCES
        ${_AS_SRC}/angelscript/source/as_callfunc_x64_msvc_asm.asm)
endif()

add_library(angelscript STATIC ${_AS_SOURCES})
target_include_directories(angelscript PUBLIC
    ${_AS_SRC}/angelscript/include
)
target_compile_definitions(angelscript PRIVATE
    -D_CRT_SECURE_NO_WARNINGS
    -DANGELSCRIPT_EXPORT
    -D_LIB
)
find_package(Threads QUIET)
if(TARGET Threads::Threads)
    target_link_libraries(angelscript PUBLIC Threads::Threads)
endif()

# Standard-string add-on — needed for AS `string` type.
# Expose add_on/ publicly so consumers can #include <scriptstdstring/...>.
add_library(snt_as_scriptstdstring STATIC
    ${_AS_SRC}/add_on/scriptstdstring/scriptstdstring.cpp
)
target_include_directories(snt_as_scriptstdstring PUBLIC
    ${_AS_SRC}/angelscript/include
    ${_AS_SRC}/add_on
)
target_link_libraries(snt_as_scriptstdstring PUBLIC angelscript)

# Scriptbuilder add-on — needed for #include resolution.
# Expose add_on/ publicly so consumers can #include <scriptbuilder/...>.
add_library(snt_as_scriptbuilder STATIC
    ${_AS_SRC}/add_on/scriptbuilder/scriptbuilder.cpp
)
target_include_directories(snt_as_scriptbuilder PUBLIC
    ${_AS_SRC}/angelscript/include
    ${_AS_SRC}/add_on
)
target_link_libraries(snt_as_scriptbuilder PUBLIC angelscript)

# Debugger add-on.
# Expose add_on/ publicly so consumers can #include <debugger/...>.
add_library(snt_as_debugger STATIC
    ${_AS_SRC}/add_on/debugger/debugger.cpp
)
target_include_directories(snt_as_debugger PUBLIC
    ${_AS_SRC}/angelscript/include
    ${_AS_SRC}/add_on
)
target_link_libraries(snt_as_debugger PUBLIC angelscript)

# ============================================================
# Helper: target link to all engine third-party
# ============================================================
add_library(snt_third_party INTERFACE)
target_link_libraries(snt_third_party INTERFACE
    Vulkan::Headers
    volk
    vma_config
    EnTT::EnTT
    stb::stb
    nlohmann_json::nlohmann_json
    glm::glm
    tinyobjloader::tinyobjloader
    angelscript
    snt_as_scriptstdstring
    snt_as_scriptbuilder
    snt_as_debugger
)
target_link_libraries(snt_third_party INTERFACE
    Freetype::Freetype
    snt_icu
    snt_harfbuzz
    shaderc::shaderc_shared
)
if(TARGET SDL3-shared)
    target_link_libraries(snt_third_party INTERFACE SDL3-shared)
endif()

# ============================================================
# GoogleTest (unit testing framework)
# ============================================================
# Fetched unconditionally so the test target can wire it up when
# SNT_BUILD_TESTS=ON. The library itself is only built when tests are
# enabled (see EXCLUDE_FROM_ALL below + snt_tests target wiring).
option(SNT_BUILD_TESTS "Build unit tests" ON)
if(SNT_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        URL ${_SNT_DOWNLOADS_DIR}/googletest-v1.14.0.zip
    )
    # Prevent GoogleTest from overriding our parent project's compiler
    # options (it tries to enable some warnings we don't want).
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
