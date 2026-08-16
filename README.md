# efs

A Windows desktop file-search UI built on the Everything search engine.
UI language: English. x64 only.

Status: **Phase 0 (walking skeleton) complete.** See
`C:\Users\lambe\.claude\plans\windows-everything-file-swirling-canyon.md` for the
full plan; this README records only what Phase 0 established.

## Toolchain (exact versions used)

| Component | Version |
|---|---|
| Qt | **6.8.3** (LTS), `win64_msvc2022_64`, installed via `aqtinstall` 3.3.0 to `C:\Qt\6.8.3\msvc2022_64` |
| Visual Studio | Community 2022, 17.14.36518.9 (MSVC 19.44.35217.0, toolset 14.44.35207) |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.0.3 |
| Everything | 1.4.1.1022 (`C:\Program Files\Everything\Everything.exe`) |
| Everything SDK | voidtools `Everything-SDK.zip`, header + `dll\Everything64.dll` vendored into `third_party/everything-sdk/` |

Qt was installed with:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

## Build and run

```powershell
cmake --preset msvc2022-x64
cmake --build --preset msvc2022-x64-debug      # or msvc2022-x64-release

$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"   # until windeployqt (Phase 3)
.\build\msvc2022-x64\Debug\efs.exe
.\build\msvc2022-x64\Debug\efs_spike.exe
```

The Visual Studio generator is multi-config, so there is one configure preset
(`msvc2022-x64`) and two build presets. `Everything64.dll` is copied next to each
executable by a post-build step.

## Targets

| Target | Purpose |
|---|---|
| `efs_core` | static lib; currently only `EverythingApi` (the DLL boundary). Grows into `core/` + `backend/` in Phase 1. |
| `efs` | WIN32 GUI executable. Phase 0: an empty `QMainWindow`. |
| `efs_spike` | **Phase 0 only.** Console probe for the Everything SDK and the `ext:`/`regex:` question. Delete when Phase 1 lands the real backend. |

`ctest` is not wired up yet — there are no tests until Phase 1.

## Phase 0 findings

### `ext:` + `regex:` composability — PASS

Plan section 6.2 flagged a risk: `Everything_SetRegex(TRUE)` makes the *whole*
search string a regex, which would be incompatible with an `ext:` prefix. The
spike confirms both halves of that concern on Everything 1.4.1.1022:

- **The inline `regex:` modifier composes with `ext:`.** `ext:jpg regex:^a00`
  returned 122 results, every one of which ends in `.jpg` **and** matches
  `^a00`; the negative control `ext:jpg regex:^ZZQXNOMATCH` returned 0. The
  terms are AND-ed.
- **The global flag is the wrong lever.** `Everything_SetRegex(TRUE)` with
  `ext:jpg ^IMG_\d+` returned 0 results — the `ext:` prefix is swallowed into
  the pattern, exactly as suspected.

**Decision: `EverythingQueryBuilder` keeps `Everything_SetRegex(FALSE)`
permanently and emits `"<extPrefix> regex:<pattern>"`.** The fallback described
in the plan (folding extensions into the regex) is *not* needed and must not be
implemented.

### Other observations

- IPC reaches the running client: `Everything_GetMajorVersion` etc. report
  `1.4.1.1022`.
- Truncation behaves as the plan assumes: `ext:jpg` on this machine gives
  `GetNumResults=5000` (the `SetMax` cap) against `GetTotResults=1288529`, so
  `truncated = totalMatches > rows.size()` is a sound test.
- Matching is case-insensitive with `SetMatchCase(FALSE)`: `regex:^IMG_\d+`
  matched `img_0193`.
- Missing-DLL diagnosis works: running the spike from a directory without
  `Everything64.dll` reports the probed paths and win32 error 126 rather than
  failing to start.

## Decisions fixed for the MVP

- App/exe name `efs`; UI in English.
- `maxResults` = 5000; `matchPath` = false; `matchCase` = false.
- File-kind extension lists are hard-coded in source.
- Everything is **not** auto-started when it is not running; the UI explains why
  the search failed.
- Everything 1.5 is out of scope; no 1.4/1.5 abstraction is introduced.
