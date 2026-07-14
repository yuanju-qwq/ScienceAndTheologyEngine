# ScienceAndTheologyEngine

ScienceAndTheologyEngine 是 Science & Theology 使用的 C++20 运行时。它提供 SDL/Vulkan-free 的 `snt_simulation_runtime`（路径、内容、作业、脚本、ECS 与通用 voxel 数据）以及组合它的 `snt_client_runtime`（平台、输入、GPU 资产、渲染和 UI）。它是一个库仓库，不提供游戏可执行程序或游戏内容包。

## 环境要求

当前支持的开发环境是 Windows 10/11 x64 和 PowerShell 7。

- Git
- CMake 3.20 或更高版本
- Visual Studio 2019 或 2022，并安装“使用 C++ 的桌面开发”工作负载
- Vulkan SDK：设置 `VULKAN_SDK`，并确保 SDK 的 `Include`、`Lib` 和 `Bin` 目录中提供 `shaderc_shared`

CMake 所需的第三方源码压缩包已跟踪在 `third_party/_downloads`。正常克隆后不需要运行依赖下载脚本；SDL3 会在第一次配置时解压到 CMake 构建目录，源码树不会产生解压后的依赖文件。

## 编译引擎库

克隆引擎后，按宿主类型构建对应静态库目标：

```powershell
git clone https://github.com/yuanju-qwq/ScienceAndTheologyEngine.git
Set-Location ScienceAndTheologyEngine
cmake -S . -B build -DSNT_BUILD_TESTS=OFF
cmake --build build --target snt_simulation_runtime --config Debug
cmake --build build --target snt_client_runtime --config Debug
```

`snt_simulation_runtime` 适合 dedicated server、确定性测试和无头宿主；`snt_client_runtime` 用于图形客户端，并会链接前者。使用 Visual Studio 生成器时，生成的库位于 `build/engine/Debug/`。两者都是静态库，因此该仓库不会生成可运行的游戏程序。

配置阶段会自动审计引擎模块的 `#include "module/..."` 关系。每个内部模块 include 都必须由消费者直接在 `target_link_libraries` 中声明；漏声明会在生成构建文件前失败，而不是依赖最终可执行程序偶然补齐。

从同一构建目录编译 Release：

```powershell
cmake --build build --target snt_simulation_runtime snt_client_runtime --config Release
```

## 运行测试

启用测试时使用独立构建目录：

```powershell
cmake -S . -B build-tests -DSNT_BUILD_TESTS=ON
cmake --build build-tests --target snt_tests snt_simulation_runtime_tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

`snt_tests` 覆盖其余引擎模块；`snt_simulation_runtime_tests` 只链接模拟闭包，验证启动、固定 tick 与关闭不会引入 SDL、Vulkan 或 client runtime。

## 嵌入游戏

游戏仓库将此引擎作为子模块，并将两个项目加入 CMake：

```cmake
add_subdirectory(snt_engine)
add_subdirectory(game)
```

图形客户端链接 `snt_client_runtime`，并用显式的 `snt::core::RuntimePaths`、`RuntimeConfig` 和游戏实现的 `IClientSession` 初始化 `snt::engine::ClientRuntime`。无头宿主只链接 `snt_simulation_runtime`，并用 `ISimulationSession` 初始化 `snt::engine::SimulationRuntime`：

```cmake
target_link_libraries(game_client PRIVATE snt_client_runtime)
target_link_libraries(game_server PRIVATE snt_simulation_runtime)
```

- `engine_root`：着色器和 ICU 数据等已打包的引擎资源。
- `game_root`：游戏拥有的配置、场景、脚本和资产。
- `user_root`：可写的日志、存档和缓存目录。

典型 client 调用为 `client_runtime.init(runtime_config, paths, std::make_unique<ClientSession>())`，无头调用为 `simulation_runtime.init(runtime_config, paths, std::make_unique<SimulationSession>())`；两者随后调用 `run` 与 `shutdown`。运行时绝不会从当前工作目录、父目录或子模块名称推断这些路径。脚本、确定性玩法和 world 创建属于 simulation session；场景、玩家输入、相机和玩法 UI 只属于 client session。

## 许可证

代码和资源使用 [PolyForm Noncommercial License 1.0.0](LICENSE)。商业使用需向 yuanju（2358586959@qq.com）单独取得授权。
