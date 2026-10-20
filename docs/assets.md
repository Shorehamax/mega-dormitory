# Art sources and licences

The following assets are bundled locally and used in the native scene. They are free CC0 assets from Poly Haven; Mega Dormitory does not claim authorship of them. The game does not query the live Poly Haven API.

| Asset | Creator(s) | Licence |
|---|---|---|
| [Old Bed Frame](https://polyhaven.com/a/old_bed_frame) | Luca B | [CC0 1.0](https://polyhaven.com/license) |
| [Plastic Monobloc Chair 01](https://polyhaven.com/a/plastic_monobloc_chair_01) | Kuutti Siitonen | [CC0 1.0](https://polyhaven.com/license) |
| [Painted Wooden Table](https://polyhaven.com/a/painted_wooden_table) | Kirill Sannikov | [CC0 1.0](https://polyhaven.com/license) |
| [Metal Trash Can](https://polyhaven.com/a/metal_trash_can) | GurJas Studios | [CC0 1.0](https://polyhaven.com/license) |
| [Painted Wooden Cabinet 02](https://polyhaven.com/a/painted_wooden_cabinet_02) | Kirill Sannikov | [CC0 1.0](https://polyhaven.com/license) |
| [Wheelchair 01](https://polyhaven.com/a/wheelchair_01) | Garreth Dean | [CC0 1.0](https://polyhaven.com/license) |
| [Concrete Floor 02](https://polyhaven.com/a/concrete_floor_02) | Poly Haven contributors | [CC0 1.0](https://polyhaven.com/license) |

Original glTF meshes, binary buffers, and 1K material maps are preserved. The source/download URLs and SHA-256 hashes are in `assets/models/manifest.json` and `assets/surfaces/manifest.json`. The concrete plane is an original project fixture using the downloaded concrete maps.

Run `python3 tools/verify_assets.py` to verify the bundled art. The retained original downloads can be replaced with higher resolution variants later without changing the simulation. The current material atlas uses 768-pixel tiles in a 3,840 × 3,840 atlas.

## Import capabilities and limits

- Native loader: glTF 2.0 and binary GLB, node transforms, indexed/non-indexed triangles, normal transforms, UVs, base colour/normal/roughness/metalness maps, and embedded/local images.
- OBJ: geometry, UVs, normals, negative indices and fan triangulation. OBJ material-library import is not yet implemented.
- Image decoding: JPEG, PNG, TGA, BMP and the other formats supported by bundled stb_image. The current material path expects ordinary colour/normal/ORM maps, not compressed GPU texture containers.
- The renderer uses a 25-material atlas. The shipped art uses 18 materials. The importer supports joint hierarchies, inverse-bind matrices and up to eight normalized influences per vertex. Imported animation clips, morph targets, Draco/KTX2, texture-transform extensions, complete transparent-material handling, FBX and USD import are not implemented. High-detail props are restricted to the selected floor; full GPU mesh instancing and LOD baking remain to be implemented.
- Geometry/image decode uses small MIT/public-domain utilities (`cgltf`, `stb_image`). Vulkan rendering and the game engine remain project code. Utility source URLs, checksums and licence files are in `third_party/`.
- Noto Sans CJK is bundled under the SIL Open Font License. See `assets/fonts/LICENSE.txt`.

Lighting now includes material roughness/metalness, normal mapping, ambient occlusion maps, directional and local ceiling-light shading, filmic tone mapping, and depth fog. A camera-local directional shadow map uses depth bias and 3 × 3 comparison filtering (2,048 pixels on GPU, 512 on software). Full indirect illumination, shadows for every ceiling light and final character art remain pending.

## Vehicles and modular customization

[Kenney Car Kit 3.1](https://kenney.nl/assets/car-kit) is bundled under CC0. The sedan, van and ambulance are rendered in the game; two replacement wheel meshes are retained as source assets for later wheel-fitting work. These are stylized models, not photorealistic vehicles. Original GLB files, palette texture, licence and SHA-256/source records live in `assets/vehicles/kenney/`.

The importer retains named mesh parts and pivots. Cabinet doors rotate about their original hinge, the drawer translates independently, and vehicle wheels remain separate from body paint. Bed frames, linens, blankets, cabinet finish/opening, clothes and owned vehicle options are saved independently. GPU mipmaps now reduce material shimmer. The active atlas has 18 materials across twelve loaded source assets plus two derived crowd LODs.

## Resident bodies

Quaternius’s [Universal Base Characters](https://quaternius.com/packs/universalbasecharacters.html) free Standard pack supplies the male and female rigged base bodies. Original glTF files, buffers, textures, the author's CC0 licence and 17 file hashes are in `assets/characters/quaternius/`. Two texture filenames referenced by the original glTF were missing from its archive; the corresponding original PNGs are copied under those names, recorded in `source.json`. The source models are otherwise unchanged. The entire bundled art verifier now checks 59 files.

The engine imports their joint hierarchies and inverse-bind matrices, skins positions/normals, and blends procedural idle, walk, sit, eat, sleep, climb, carrying, work, combat and surrender poses. Nearby characters use these bodies (up to 64 on the GPU path, eight on software; at most four nearby full-resolution bodies); distant crowds retain simpler figures. The simulation still tracks everyone. Current skinning runs on the CPU. Per-character clothing colors, cleanliness, jacket/pocket details and accessories remain separate from immutable body geometry. Fabric regions blend over the body material; these are not fully modeled garments. Hair caps are project geometry.

The asset tests verify both bodies’ bind-pose fidelity and activity bounds, plus an independent two-joint weighted deformation fixture. Full clip import, retargeting, body morphs, modular garment meshes, cloth simulation, hand/contact IK and production-quality animation remain unfinished.

Mid-distance residents use deterministic vertex-clustered skeletal LODs made by project code. Material/UV regions and skin weights are retained; source geometry stays immutable. Both detail levels are exercised by the pose tests. Geometry storage is reused across frames; clothes masks are precomputed once per model.
