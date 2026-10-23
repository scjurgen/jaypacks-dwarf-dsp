# dev-scripts

Optional one-step wrappers around plain CMake, CTest and pip. Each script does one thing,
keeps its full log in `build-tests/scratch/` and prints only a short result (the last lines
of an error, the warning list, the test summary). Nothing here is required: every script is a
few lines of shell, and the plain commands below do the same.

Why they exist: a short, fixed command is easy to repeat by hand, and an LLM session can be
allowed to run it once instead of asking for every `cmake` and `ctest` chain. The usage of
each script and the daily loop are in `docs/building.md`.

| Script | Plain equivalent |
|---|---|
| `dev-configure.sh` | `cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DJP_BUILD_TESTS=ON` (creates the venv first) |
| `dev-build.sh [target...]` | `cmake --build build-tests --target <targets, default all>` (configures first if needed) |
| `dev-test.sh [regex]` | build, then `ctest --test-dir build-tests --output-on-failure [-R regex]` |
| `dev-warnings.sh` | clean rebuild of `build-tests/`, then the compiler warnings in own code (outside `third_party/` and `_deps/`) |
| `dev-explore.sh [file] [--plugin NAME]` | `clang++ -std=c++17 -I. [-Iplugins/NAME] file -o explore/<name>`, then run it |
| `dev-venv.sh` | `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` (only when missing or changed); prints the venv's python |
| `dev-docs-plots.sh [folder]` | configure `build-docs/` with `-DJP_BUILD_DOCS=ON`, build `docs_explore`, run each `documentation/<folder>/plots.sh` |
| `dev-lua-editor.sh [--local]` | start lua-editor on the Dwarf (`.venv/bin/python -m lua_editor --device 192.168.51.1 --apis lua-editor/apis`); `--local` uses the sandbox; installs `lua-editor/requirements.txt` into the venv when needed |

`_env.sh` holds the shared paths and is sourced by the others, not run.
