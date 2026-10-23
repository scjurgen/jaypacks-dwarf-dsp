# Building

Everything is driven from the repository root on a Mac; no command needs a `cd`. The build
directories are listed in `docs/architecture.md`.

## Prerequisites

| Tool | For | Notes |
|---|---|---|
| CMake 3.21+ and Ninja | every build | Homebrew |
| Apple clang (Xcode command line tools) | Mac builds and tests | C++17 |
| Python 3 | generator, tools, tests | `jpgen.py` and `tools/` use only the standard library (3.6+) |
| venv `.venv` with numpy, matplotlib, Pillow | charts, modgui images (the font is in the repository) | created by `dev-scripts/dev-venv.sh` from the root `requirements.txt` (pinned versions); the other scripts call it, network needed once |
| docker with the image `dwarf-new` | Dwarf build | see below |
| Network at the first configure | GoogleTest v1.15.2 via FetchContent | |

## Daily loop

```
dev-scripts/dev-test.sh [regex]      configure build-tests/ if needed, build everything, run ctest
dev-scripts/dev-warnings.sh          clean rebuild, list warnings in own code (target: 0)
cmake --preset mac-asan && cmake --build build-mac-asan && ctest --test-dir build-mac-asan
dev-scripts/dev-explore.sh [file]    throwaway C++17 program in explore/ against dsp/
dev-scripts/dev-docs-plots.sh [dir]  rebuild and render the charts of documentation/
```

Logs of the dev-scripts go to `build-tests/scratch/`.

## Mac bundles and listening

```
tools/jp.py generate [name...]            run the generator only (validates the JSON)
tools/jp.py build-mac [name...]           release bundles in build-mac-release/lv2/
tools/jp.py build-mac [name...] --install also copy them to ~/Library/Audio/Plug-Ins/LV2
tools/jp.py snapshot [name...]            copy the modgui screenshot to docs/plugins/images/
```

Without names, every plugin in `plugins/` is built. `--install` replaces only bundles whose
manifest carries the jaypacks URI. The installed bundle loads in any LV2 host on the Mac;
testing in Reaper is described in `docs/reaper.md`. The modgui is only shown by mod-ui, not
by desktop hosts; `snapshot` copies the generated screenshot (the face as mod-ui draws it)
into `docs/plugins/images/<name>.png` for the plugin pages, the only generated file that is
committed. Without names it refreshes the existing snapshots; `build-mac` warns when one
differs from the fresh screenshot.

## Dwarf build

The cross toolchain is `moddwarf-new` of mod-plugin-builder (GCC 9.4, glibc 2.27) in a
docker image. Building the image takes about 15 minutes and needs network; the image is
about 19 GB:

```
docker build -f docker/Dockerfile -t dwarf-new docker
```

Then:

```
tools/jp.py build-dwarf [name...]
```

This configures the Mac release build if needed and runs the generator there (the device
bundle takes its TTL and modgui files from it), builds the plugin targets in the container
into `build-dwarf/` (`JP_BUILD_TESTS=OFF`, `JP_GRAPHICS=OFF`, flags from the toolchain's
`local.env`: `-ffast-math -fno-finite-math-only -mcpu=cortex-a35 ...`, plus `-O3`), and
checks each binary:

- `ports.h` and the plugin TTL of the container build equal the Mac's;
- only `lv2_descriptor` is exported (plus `_init`/`_fini`): hidden visibility, and the version
  script `lv2/exports.map` for the `std::` templates that the compiler emits out of line
  (libstdc++ gives them default visibility; `std::vector<float>::assign` showed up so);
- symbol versions at most GLIBC 2.27, GLIBCXX 3.4.28, CXXABI 1.3.12 (what the device has);
- the modgui passes `tests/host/modgui_check.py`.

The device bundles land in `build-dwarf/bundles/jaypacks-<name>.lv2`. Build logs and the
`readelf` reports are in `build-dwarf/scratch/`. The image name can be changed with
`JP_DWARF_IMAGE`.

Build code meant for the device in docker early: GCC 9 rejects some code Apple clang
accepts (`docs/caveats-and-limits.md` has an example).

## Deploy and device test

```
tools/jp.py deploy [name...] [--unload]
tools/jp.py test-device <name> [--build] [--no-deploy] [--restore]
```

`deploy` installs through mod-ui's `/sdk/install`, checks the result with `lv2info` on the
device and marks the plugin as a favourite in mod-ui (`/favorites/add`; a plugin that already
is one stays as it is), so uploaded plugins are quick to find. `test-device` and `perf`
deploy through it too, so siggen and meter become favourites as well. mod-ui refuses to replace a plugin that the loaded graph uses: `--unload` unloads the
pedalboard for the install and reloads it (an unsaved graph stays, so its plugins cannot be
replaced); `test-device` empties the graph before it deploys. `deploy`, `test-device` and
`perf` install the bundles in `build-dwarf/bundles/` as they are: after a change to the
generator or the templates, rebuild every plugin that goes to the device (`build-dwarf` with
the names, or `--build`), the test plugins included. Device address: `DWARF_HOST` (default 192.168.51.1). See `docs/device.md` and
`docs/test-plugins.md`.

## Adding a plugin

The checklist is in `docs/plugin-json.md` ("Adding a plugin").

## Regenerating the charts

`dev-scripts/dev-docs-plots.sh` builds the explore programs into `build-docs/`
(`JP_BUILD_DOCS=ON`), runs each `documentation/<folder>/plots.sh` and renders the PNGs with
`documentation/Plot/PyConPlot.py`. The PNGs and the README numbers are checked in; the data
in `generated/` is not.
