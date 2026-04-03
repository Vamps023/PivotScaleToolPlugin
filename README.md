# PivotScaleTool — Unigine 2.6 Plugin (DLL)

A plugin tool with UI that moves each selected node's pivot to its bounding-box
center and applies a uniform scale.

## Folder Structure

```
PivotScaleTool/
├── bin/                    # Build output (.dll)
├── include/
│   └── PivotScaleTool.h   # Header
├── source/
│   └── plugin.cpp          # Main plugin source
├── PivotScaleTool.sln      # Visual Studio solution
├── PivotScaleTool.vcxproj  # Visual Studio project (DynamicLibrary)
└── README.md
```

## Build Instructions

1. **Set `UNIGINE_SDK_DIR`** — either as an environment variable or edit the
   `<UNIGINE_SDK_DIR>` property in `PivotScaleTool.vcxproj` to point to your
   Unigine 2.6 SDK root (the folder containing `include/` and `lib/`).

2. Open `PivotScaleTool.sln` in Visual Studio 2017+.

3. Select **x64 / Release** (or Debug).

4. Build → the DLL is output to `bin/<Configuration>/PivotScaleTool.dll`.

## Required Libraries

| Library        | Notes                                  |
|----------------|----------------------------------------|
| `Unigine.lib`  | Core engine (linked automatically)     |

> The `.vcxproj` already lists `Unigine.lib` in `AdditionalDependencies`.
> Add `UnigineEditor.lib` if you enable the `HAS_UNIGINE_EDITOR` define for
> editor-level selection support.

## Usage

1. Copy `PivotScaleTool.dll` into your Unigine project's plugin directory.
2. Load the plugin via `-plugin PivotScaleTool` CLI flag or project settings.
3. The **"Pivot & Scale Tool"** window appears in the viewport.
4. Enter a uniform scale value (default `1.0`), select nodes, click **Apply**.

## Features

- Multi-node selection support
- Bounding-box center pivot repositioning
- Uniform scale application
- Input validation (scale > 0)
- Status feedback label
- Full logging via `Log::message` / `Log::error`
