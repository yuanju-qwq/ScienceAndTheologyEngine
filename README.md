# ScienceAndTheology Engine

The C++20 runtime for ScienceAndTheology. It owns platform integration,
rendering, ECS, data, scene serialization, scripting and engine-side tests.
It does not own a game executable, game content or a source-tree package
layout.

## Build Standalone

```powershell
cmake -S . -B build -DSNT_BUILD_TESTS=ON
cmake --build build --target snt_engine --config Debug
cmake --build build --target snt_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Embed In A Game

The game repository adds this repository as a submodule and includes it with
CMake:

```cmake
add_subdirectory(snt_engine)
add_subdirectory(game)
```

The game host links `snt_engine` and must pass `snt::core::RuntimePaths` when
calling `Engine::init`:

- `engine_root`: packaged engine resources such as shaders and ICU data.
- `game_root`: game-owned config, scenes, scripts and assets.
- `user_root`: writable logs, saves and caches.

The engine deliberately does not infer any of these paths from the current
working directory, parent folders or the submodule directory name.

## Development Contract

Engine code may read only its explicit engine root or the game/user roots
provided by the host API. Packaging, gameplay content and executable lifecycle
belong to the game repository.