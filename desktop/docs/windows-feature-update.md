# Windows feature implementation (2026-10-28)

Implementation based on main `177880f`; the validation below describes this change.

## Available controls

- Input / Camera: **White balance picker**. Click a neutral part of a Bayer RAW.
  Sampling reads black-corrected sensor values; crop, quarter turns, flips and
  zoom are inverted before the request. Cancel with the button or Escape.
  The result uses the existing undo, sidecar and RAW re-decode path. Non-Bayer,
  headroom, very dark and saturated patches are refused explicitly.
- Filmstrip: drag a visible thumbnail over another. Current selection and
  multi-selection stay unchanged. Per-folder order is stored under app data
  `FrameOrder/`; removed files are discarded and new files append on reopening.
  Saves are serialized. Source files are never renamed or moved.
- Scene Placement: adjust highlight/shadow pull-back, then **Apply**, or use
  **Auto fit**. Only a valid engine-solved curve is saved. Parameters/photo
  changes while fitting invalidate the reply. Reset and the enable switch
  preserve the usual undo behavior. This changes the negative and re-develops.
- Contrast Mask: enable explicitly and adjust highlights, shadows, midtone
  protection and radius. This is a print-side edit using the cached negative.
  It is unavailable for direct film scans, Cineon DI and positive print media.
  macOS feature flags are unchanged.

## Engine work

`spk_scene_latitude`, `spk_mask_reduce` and `spk_mask_epilogue` are Vulkan
translations of the Metal kernels. The existing contrast-mask analysis now
uses bounded standard C++ worker threads off Apple. The physical profiles,
spectral model, parameter schema and safe-math startup gate are unchanged.
The host adds the read-only `white_balance_pick` method; the UI still applies
its result through `redecode`. DNGs without a camera-table matrix can use their
embedded daylight matrix, camera calibration and analog balance. This remains
a single-illuminant approximation, not a dual-illuminant DNG profile solver.

## Validation and limits

The backend was compiled on Windows with portable LLVM-MinGW, CMake, Ninja and
glslang. New kernel float64 gates passed on Intel UHD and SwiftShader CPU Vulkan.
The full tone feature gate passed on SwiftShader: applying either feature
changes pixels; disabling restores deterministic baseline pixels; scene edits
invalidate the negative and mask edits reuse it; querying the mask does not
change the next print. Host protocol smoke and generated Bayer DNG tests cover
actual LibRaw decoding, four camera rotations, current-WB independence and
black/saturated/invalid-coordinate rejection. No private camera fixtures were used.

Frontend checks include type checking, lint, unit tests and browser interactions
for crop/turn picker coordinates, pointer reorder and persisted folder order.
Browser tests use the explicitly labelled mock host; they do not prove real GPU
image quality. The opt-in export test was also run separately on Windows with
the generated DNG and real C++ host: scene mapping and mask were enabled,
then crop, quarter turn and grading were applied before writing a ProPhoto TIFF.

Observed results:

| Gate | Result |
|---|---|
| TypeScript / ESLint / web production build | passed |
| Frontend unit tests | 63 passed; opt-in real-host test skipped in the default run |
| Real-host export test, explicitly enabled | 1 passed |
| Playwright browser regressions | 9 passed |
| Native CTest suite on SwiftShader | 17 of 18 passed; all new feature gates passed |
| Host protocol smoke | passed |

The machine's Intel UHD driver fails the project's existing fused-FMA startup
probe, so complete engine acceptance used a process-local SwiftShader ICD.
The guard was not loosened. The unchanged `spk_vulkan_pointwise` BT.709 decode
breakpoint case also fails on this software driver; do not call the entire
engine suite green. Whole-image comparison against Metal, real camera coverage
and packaged Windows GUI acceptance remain outstanding. No full GUI installer
has been produced by this change.

## Reproduction

Build the engine/host with the repository's Vulkan and LibRaw dependencies and
`SPEKTRALAB_BUILD_HOST=ON`, `SPEKTRALAB_BUILD_NATIVE_RAW=ON`, `BUILD_TESTING=ON`.
Then:

```powershell
ctest --test-dir build/windows-features --output-on-failure
python engine/tests/host_smoke.py --host build/windows-features/engine/host/spektralab-host.exe --resources build/windows-features/engine/resources
python engine/tests/host_white_balance_pick.py --host build/windows-features/engine/host/spektralab-host.exe --resources build/windows-features/engine/resources
```

If using a software ICD for verification, set `VK_DRIVER_FILES` (and, for older
loaders, `VK_ICD_FILENAMES`) to its **absolute** JSON path for that process only.
Keep the compiler's runtime DLLs available on the test process PATH. Neither
setting is an application default or a system configuration change.

```powershell
Set-Location desktop
npm ci
npm run check
npm run build:web
npm run test:e2e
```

Windows packaging still needs the real staged host and a Rust/Tauri build
environment, as documented in `scripts/package.mjs` and the desktop workflow.
The built, statically linked host and its engine resources/licenses have been
staged under `build/host-win-x64/`; this is an engine component, not an installer.
