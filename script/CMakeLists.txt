# AngelScript integration module.
#
# Wraps the raw AS SDK in engine-friendly RAII types: ScriptEngine,
# ScriptContextPool, ScriptModule, ScriptLoader, ScriptManager.
#
# This module owns generic AngelScript lifecycle and hot-reload orchestration.
# The game supplies its own IScriptContentHost for concrete content data and
# native script declarations.

add_library(snt_script STATIC
    script_engine.cpp
    script_context.cpp
    script_module.cpp
    script_loader.cpp
    script_manager.cpp
    file_watcher.cpp
)

target_include_directories(snt_script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # so #include "script/..." works
)

target_link_libraries(snt_script
    PUBLIC
        snt_core
        angelscript
        snt_as_scriptstdstring
        snt_as_scriptbuilder
        snt_as_debugger
)

#AngelScript needs winmm.lib on Windows for timeGetTime.
if(WIN32)
    target_link_libraries(snt_script PUBLIC winmm)
endif()
