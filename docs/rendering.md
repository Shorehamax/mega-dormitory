# Character rendering and compute skinning

Hardware Vulkan defaults to compute skinning for nearby rigged residents. Software Vulkan uses the existing CPU renderer. Settings can switch between GPU and CPU character animation; `--cpu-skinning` selects the reference path at launch. Simulation, navigation and economic rules are unchanged by this choice.

The four shared character meshes (male/female, full/LOD) are uploaded once, including weights, joint indices, materials, clothing masks and head-attached hair. Each resident retains its own appearance and activity. CPU code builds a compact bone palette and blended root pose; 64-wide compute groups apply up to eight weights, normal transforms, clothing colors/patterns and the final world transform to individual vertices in parallel.

The compute shader writes directly into reserved ranges of the scene vertex buffer. CPU uploads skip those ranges. A compute-write → vertex-input-read barrier makes the same generated bodies available to the shadow and scene passes. There is no per-frame vertex readback. The render buffer and persistent source data prefer coherent device-local BAR/UMA memory when available, with a normal host-visible allocation fallback. Allocation failures clean up handles before reporting an error.

Ranges, joint bounds, storage limits and overlapping dispatches are checked. If the current scene exceeds the device's storage-buffer range, the application rebuilds that frame on the CPU, switches its setting and displays a localized notice. Software rendering retains its smaller character/detail budget. This pass does not animate all 100,000 residents at full mesh detail; distant residents remain procedural and authoritative simulation still runs on the CPU.

## Verification

`--skin-test --frames 1` compares readback against the original CPU implementation for all ten activity poses, both body models, both detail levels and all three clothing patterns, including blended poses. It checks 3,239,190 vertices and all 16 packed vertex components. Hardware RTX and lavapipe both reported a maximum component difference of `9.53674e-07` in those cases. An additional multi-character batch checks separate palettes/output ranges, high-floor coordinates and rejection of overlapping output ranges.

Display-dependent integration checks are optional:

```bash
cmake -S . -B /path/to/build -DMEGA_VULKAN_TESTS=ON
cmake --build /path/to/build
ctest --test-dir /path/to/build --output-on-failure
```

They require a desktop display, Vulkan driver/software driver and the Vulkan validation layer. Validation errors fail these checks. Ordinary headless simulation/asset/UI/localization tests do not require a display.

A fixed 120-frame 600-person crowd sequence at 1200 × 800 on the development RTX 5070 submitted 741,132 GPU-skinned vertices in its final frame. GPU/CPU scene construction took 0.645/2.922 seconds; total render loops took 2.348/3.879 seconds. Including startup/capture, runs took 3.63/5.15 seconds. Both paths used the same detail budget and fixed timestep, with validation enabled. These are bounded local comparisons, not general FPS or hardware guarantees. An earlier system-memory output attempt was slower overall and was replaced with the local-memory preference described above.

The changed asset/UI paths pass address, undefined-behavior and leak checking. Vulkan palette/readback and live rendering also passed address/undefined-behavior checks with driver leak detection disabled; graphics-driver allocations are outside that leak check. A forced tiny storage budget verifies automatic CPU fallback. This remains a procedural posing system: imported animation clips, retargeting, cloth simulation, contact IK and broader geometry streaming are unfinished.
