# Language-neutral ABI boundary.
#
# This target deliberately contains only C-compatible value contracts and a
# descriptor query. It is intentionally a pure-C leaf library: linking a Zig
# host to it must not pull in the C++ runtime, ECS, GPU, allocator, or
# exception types. Future native hosts consume it through abi/ headers.
add_library(snt_abi STATIC
    runtime_abi.c
)
target_include_directories(snt_abi PUBLIC
    "${SNT_ENGINE_SOURCE_DIR}"
)

# The implementation uses no CRT service. Omitting MSVC's implicit default
# library directive keeps this C-only archive linkable by Zig/LLD as well as
# the normal MSVC host; the final executable supplies its own runtime.
if(MSVC)
    target_compile_options(snt_abi PRIVATE /Zl)
endif()
