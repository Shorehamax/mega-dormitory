# Native release preparation

The current build is a development candidate. The accepted backlog, reference-quality interiors, production animation/audio, broader disaster/physics systems and final balancing remain release blockers in `status.md`.

## Verify and package

Configure a release build with `MEGA_VULKAN_TESTS=ON`, build it, and run CTest with a display and Vulkan validation available. Run the separate address/undefined-behavior/leak build as well. `tools/check_locales.py` and `tools/verify_assets.py` check catalogues and downloaded art. Keep the results with the candidate.

For a verified archive, run `python3 tools/package_candidate.py --build-dir <build-directory> --output-dir <candidate-directory>`. This rebuilds, runs the complete native/rendered CTest set and catalogue/asset checks, installs into temporary staging, verifies relocated resource discovery and emits an archive with an integrity manifest, checksum and check log. Test saves use temporary storage; player slots are not loaded. Memory checks remain a separate required step.

For installation-only packaging, run `cpack --config <build-directory>/CPackConfig.cmake`. The archive includes the executable, assets, compiled shaders, runtime-layout marker, usage docs and utility/asset licence notices. It does not bundle graphics drivers or system libraries. On Linux the player needs the Vulkan loader and a compatible hardware driver or CPU Vulkan driver, GLFW 3.3+, Zstandard and zlib. A software renderer can be selected with `--software`.

Extract the archive to a different directory, including a path with spaces. From an unrelated working directory run the extracted `mega_dormitory --verify-runtime`, then start the game. Verify the menu, all three languages, keyboard/mouse input, a new survival session, named save/reload and the selected rendering device. The installed package resolves resources beside its executable and rejects a marked incomplete installation. `MEGA_DATA_DIR` can explicitly select a complete runtime root; an invalid override fails clearly.

Installed saves use `$XDG_DATA_HOME/mega-dormitory`, `$LOCALAPPDATA/mega-dormitory` or `~/.local/share/mega-dormitory`. Moving/replacing the installation does not move saves. Source-tree launches retain `<project>/userdata`, and `--save-root` can select a different location. Packaging never imports, deletes or overwrites existing slots. Copy wanted older slots deliberately while the game is closed.

## Release decisions

A distributable archive is a packaging check, not a finished-game declaration. A production release also requires sustained scarcity/economy testing, compatibility across the supported save matrix, actual manual play sessions, accessibility/input coverage, crash recovery and supported-platform hardware testing. Windows packaging and the remaining gameplay/art backlog are still outstanding. Do not label a candidate final based only on automated pass counts.
