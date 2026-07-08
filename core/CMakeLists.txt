# Core: Job System + Logger + Memory Tracker + Profiling (foundational utilities).
# - Job System: P1 serial stub. P2 will replace with real thread pool.
# - Logger: lightweight printf-style logging with runtime level filter.
# - Memory Tracker: global new/delete overloads + ScopeAllocator for delta
#   tracking. Lives in core so every layer above (and third-party libs) is
#   counted automatically.
# - Profiling: header-only macros with pluggable backend (NONE/TRACY/OPTICK).

# ---------------------------------------------------------------------------
# Profiling backend selection.
# ---------------------------------------------------------------------------
# SNT_PROFILE_BACKEND accepts NONE / TRACY / OPTICK. The default is NONE so
# shipping builds pay zero cost. Flip to TRACY or OPTICK at configure time
# without touching call sites; the link target for the chosen backend must
# be available (provided by fetch_third_party.cmake when enabled).
set(SNT_PROFILE_BACKEND "NONE" CACHE STRING "Profiling backend: NONE / TRACY / OPTICK")
set_property(CACHE SNT_PROFILE_BACKEND PROPERTY STRINGS NONE TRACY OPTICK)

# Translate the string option to exactly ONE SNT_PROFILE_BACKEND_* compile
# definition (value 1). The inactive backends are left UNDEFINED so that
# profiling.h's `#if defined(...)` checks resolve correctly. (Defining a
# macro to 0 still counts as "defined" in the preprocessor, so we must
# avoid emitting 0s for inactive backends.)
if(SNT_PROFILE_BACKEND STREQUAL "TRACY")
    target_compile_definitions(snt_engine_settings INTERFACE SNT_PROFILE_BACKEND_TRACY=1)
elseif(SNT_PROFILE_BACKEND STREQUAL "OPTICK")
    target_compile_definitions(snt_engine_settings INTERFACE SNT_PROFILE_BACKEND_OPTICK=1)
else()
    target_compile_definitions(snt_engine_settings INTERFACE SNT_PROFILE_BACKEND_NONE=1)
endif()

add_library(snt_core STATIC
    job_system.cpp
    log.cpp
    memory_tracker.cpp
    engine_config.cpp
    path_utils.cpp
    uuid.cpp
)
target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_core PUBLIC snt_engine_settings)
# engine_config.cpp uses nlohmann_json for JSON parsing. Linked PRIVATE
# so the dependency doesn't leak to consumers of snt_core (the header
# itself is dependency-free).
target_link_libraries(snt_core PRIVATE snt_third_party)
