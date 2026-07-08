# Third-party dependencies managed as git submodules.
#
# Setup:
#   git submodule add https://github.com/libsdl-org/SDL.git snt_engine/third_party/SDL
#   cd snt_engine/third_party/SDL && git checkout release-3.2.x
#
# Header-only libraries (VMA, EnTT, stb, nlohmann_json, vulkan-headers)
# are NOT submodules — they are fetched by ../cmake/fetch_third_party.cmake.

# SDL3 submodule target is configured from ../CMakeLists.txt.
# Add other heavy libs (shaderc) here as they become needed in later phases.
