# CLAUDE.md

## Project overview
- This repo is a Windows-only Direct3D 12 research/demo application.
- Main authored code lives under `D3D12/`.
- Build/project generation source of truth is `scripts/premake5.lua`, not generated Visual Studio files.
- `README.txt` notes this project was learned from `simco50/D3D12_Research`.

## Build and generation
- Generate Visual Studio files with `scripts/generate_vs2022.bat`.
- That script runs `premake5 vs2022`.
- Premake workspace/project name is `D3D12`.
- Configurations: `Debug`, `Release`, `DebugASAN`.
- Platform: `x64`.
- Language standard: C++20.
- App kind: `WindowedApp`.
- Build outputs go under:
  - `Build/$(ProjectName)_$(Platform)_$(Configuration)`
  - `Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)`
- Premake copies `D3D12/Resources` into the output directory after build.
- Premake also copies required runtime DLLs for D3D12 Agility SDK, PIX, DXC, and Optick.

## Where to start reading
- `D3D12/main.cpp`
  - Win32 entry point, window creation, message loop, command-line parsing, console/CVar/task queue init.
- `D3D12/DemoApp.h`
  - Top-level application state and owned rendering systems.
- `D3D12/DemoApp.cpp`
  - Graphics initialization, asset loading, scene setup, resize/update flow, render-path orchestration.
- `D3D12/Graphics/Core/`
  - Low-level D3D12 abstraction: device, swap chain, command contexts, resources, descriptors, root signatures, PSOs, shaders.
- `D3D12/Graphics/RenderGraph/`
  - Render graph types, pass/resource lifetime, graph execution, graph export.
- `D3D12/Graphics/Techniques/`
  - Concrete rendering features such as clustered/tiled forward, path tracing, SSAO, RT reflections, particles, clouds, CBT tessellation.
- `D3D12/Core/`
  - Platform/utilities: command line, console variables, input, file watching, task queue, paths.
- `D3D12/Scene/`, `D3D12/Content/`, `D3D12/Graphics/`
  - Scene types, asset/content loading, higher-level rendering helpers.

## Runtime behavior and important paths
- Runtime resource root is `Resources/`.
- Shader root is `Resources/Shaders/`.
- Default scene currently loaded in `D3D12/DemoApp.cpp` is `Resources/Scenes/Sponza/Sponza.gltf`.
- `D3D12/Core/Paths.cpp` defines these important paths relative to the working directory:
  - `Saved/`
  - `Saved/Logs/`
  - `Saved/Profiling/`
  - `Saved/Config/`
  - `Saved/ShaderCache/`
  - `Screenshot/`
  - `Paks/`
  - `Resources/`
  - `Resources/Shaders/`
- ImGui state is written to `Saved/imgui.ini` in `D3D12/Graphics/ImGuiRenderer.cpp`.

## Important systems
- `DemoApp` is the top-level coordinator. It creates the graphics instance/device/swap chain, initializes techniques, loads assets, sets up the scene, and drives frame updates.
- `RenderPath` options are declared in `D3D12/DemoApp.h`; the default is `RenderPath::Clustered`.
- `D3D12/Graphics/Core/Graphics.cpp` supports debug-related command-line flags such as:
  - `d3ddebug`
  - `dred`
  - `gpuvalidation`
  - `pix`
  - `warp`
  - `d3dbreakvalidation`
  - `stablepowerstate`
- `D3D12/Graphics/RenderGraph/RenderGraph.h` exposes both `DumpGraphViz(...)` and `DumpGraphMermaid(...)`.
- `D3D12/Graphics/ImGuiRenderer.cpp` initializes ImGui, loads `Resources/Fonts/Roboto-Bold.ttf`, and renders UI through the render graph.

## Editing guidance
- Prefer editing authored source under `D3D12/` and build scripts under `scripts/`.
- Prefer `scripts/premake5.lua` when build/project structure changes are needed.
- Do not treat generated Visual Studio artifacts as source of truth.
- Avoid editing generated or local-state paths unless the task explicitly requires it:
  - `Build/`
  - `.vs/`
  - `ipch/`
  - generated solution/project files
  - copied runtime DLLs in build outputs
  - `imgui.ini` state files
- Vendor code lives under `D3D12/External/`; avoid changing it unless the task is explicitly about third-party dependencies.

## Validation guidance
- For documentation-only changes, verify every claim against source files.
- For code changes that affect build structure, re-check `scripts/premake5.lua` and regenerate project files.
- For rendering/runtime changes, expect validation to be mostly manual: build, launch the app, and smoke-test the affected path.
- This repo does not currently show an obvious automated test suite in the inspected DX12 project files, so do not assume tests exist.
