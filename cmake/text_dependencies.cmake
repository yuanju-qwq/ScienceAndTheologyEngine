# Unicode text dependencies for the retained MUI P6 path.
#
# HarfBuzz and ICU4C are vendored below third_party/ with their upstream
# license files. They are deliberately built as static libraries so runtime
# text shaping has no dependency on Godot or on a platform text API.

set(_SNT_TEXT_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(_SNT_HARFBUZZ_DIR "${_SNT_TEXT_VENDOR_DIR}/harfbuzz")
set(_SNT_ICU_DIR "${_SNT_TEXT_VENDOR_DIR}/icu4c")

if(NOT EXISTS "${_SNT_HARFBUZZ_DIR}/src/hb.h")
    message(FATAL_ERROR "P6 requires vendored HarfBuzz at ${_SNT_HARFBUZZ_DIR}")
endif()
if(NOT EXISTS "${_SNT_ICU_DIR}/common/unicode/ubidi.h")
    message(FATAL_ERROR "P6 requires vendored ICU4C at ${_SNT_ICU_DIR}")
endif()

# The Godot-maintained ICU4C source snapshot is a compact static build. Build
# common + i18n together because P6 needs BiDi, grapheme segmentation and
# locale-aware line breaking; a partial Unicode implementation is forbidden.
file(GLOB _SNT_ICU_COMMON CONFIGURE_DEPENDS "${_SNT_ICU_DIR}/common/*.cpp")
file(GLOB _SNT_ICU_I18N CONFIGURE_DEPENDS "${_SNT_ICU_DIR}/i18n/*.cpp")
list(APPEND _SNT_ICU_COMMON "${_SNT_ICU_DIR}/icu_data/icudata_stub.cpp")
add_library(snt_icu STATIC ${_SNT_ICU_COMMON} ${_SNT_ICU_I18N})
target_include_directories(snt_icu PUBLIC
    "${_SNT_ICU_DIR}/common"
    "${_SNT_ICU_DIR}/i18n"
)
target_compile_definitions(snt_icu PRIVATE
    U_STATIC_IMPLEMENTATION
    U_COMBINED_IMPLEMENTATION
    U_COMMON_IMPLEMENTATION
    U_I18N_IMPLEMENTATION
    UCONFIG_NO_COLLATION
    UCONFIG_NO_CONVERSION
    UCONFIG_NO_FORMATTING
    UCONFIG_NO_SERVICE
    UCONFIG_NO_IDNA
    UCONFIG_NO_TRANSLITERATION
    UCONFIG_NO_REGULAR_EXPRESSIONS
    U_ENABLE_DYLOAD=0
    U_HAVE_LIB_SUFFIX=1
    U_LIB_SUFFIX_C_NAME=_snt
)
target_link_libraries(snt_icu PUBLIC snt_engine_settings)
target_compile_definitions(snt_icu PUBLIC
    U_STATIC_IMPLEMENTATION
    U_HAVE_LIB_SUFFIX=1
    U_LIB_SUFFIX_C_NAME=_snt
)

# HarfBuzz has no stable CMake project in this source snapshot. All source
# files are compiled with optional platform adapters disabled by their own
# feature guards; hb-ft and hb-icu are enabled for the MUI text backend.
file(GLOB _SNT_HARFBUZZ_SOURCES CONFIGURE_DEPENDS "${_SNT_HARFBUZZ_DIR}/src/*.cc")
list(APPEND _SNT_HARFBUZZ_SOURCES "${_SNT_HARFBUZZ_DIR}/src/OT/Var/VARC/VARC.cc")
add_library(snt_harfbuzz STATIC ${_SNT_HARFBUZZ_SOURCES})
target_include_directories(snt_harfbuzz PUBLIC "${_SNT_HARFBUZZ_DIR}/src")
target_compile_definitions(snt_harfbuzz PRIVATE HAVE_FREETYPE HAVE_ICU)
target_link_libraries(snt_harfbuzz PUBLIC
    snt_engine_settings
    snt_icu
    Freetype::Freetype
)

if(MSVC)
    target_compile_options(snt_icu PRIVATE /MP)
    target_compile_options(snt_harfbuzz PRIVATE /MP)
endif()

