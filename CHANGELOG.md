# Changelog

All user-facing changes to the Blamdom Launcher.

**Release rule:** every version gets an entry here *before* its tag is pushed —
CI copies the matching `## <version>` section into the GitHub release notes,
so a release without real notes means an entry was skipped.

## 0.1.8 — 2026-11-21

### Changed

- The Windows build now embeds the Blamdom icon (our grass block) in
  `blamdom-launcher.exe`. Previously the .exe showed the upstream
  Prism Launcher icon.

## 0.1.7 — 2026-11-19

### Added

- **Windows x64 release.** Self-contained zip: `blamdom-launcher.exe`, the
  `prismlauncher_filelink.exe` helper, the bundled Java helper jars, all
  Qt/runtime DLLs and the Windows platform plugin. Extract and run — no
  installer, no admin rights, no per-machine changes.
- Both platform builds now land in a single GitHub release with one merged
  `SHA256SUMS` for checksum verification.
- README installation instructions for Windows.

### Fixed

- Windows build pipeline: Qt install path, multi-config build targets
  (`BlamdomLauncher` + `BlamdomLauncher_filelink`), and the tag/version
  consistency check.

## 0.1.6 — 2026-11-18

### Fixed

- In-instance **Java install** and **pack updates** failed on fresh
  installs because the release tarball was missing the helper jars
  (`JavaCheck.jar`, `NewLaunch.jar`, `NewLaunchLegacy.jar`) that the
  launcher spawns from its own directory. They are now bundled in the
  Linux release.

## 0.1.5 — 2026-11-18

### Added

- **Install Java in the instance.** When a pack needs a Java major version
  that isn't installed, the instance settings show exactly which Java the
  pack needs and install it with one click (Adoptium download).
- **Update the pack in place.** Instances installed from a Blamdom pack
  detect newer pack versions on mine.blamdom.dev and update without
  re-importing.

## 0.1.4 — 2026-11-17

### Fixed

- When the self-updater can't update itself, it now points you at the
  per-user installer (no admin required) instead of dead-ending.

## 0.1.3 — 2026-11-17

### Changed

- Shipped **Cozy Night** — the launcher's permanent look: midnight-teal
  palette with ember-amber accents across the whole UI.

## 0.1.2 — 2026-11-17

### Fixed

- The Blamdom pack selector: our packs now auto-select, show their details
  (version, size, sha256), and instance creation works with the pack
  selected.

## 0.1.1 — 2026-11-17

### Changed

- Full UI re-theme to the Blamdom design reference (intermediate step
  before Cozy Night).

## 0.1.0 — 2026-11-16

### Added

- First public release of the Blamdom Launcher — a fork of
  [Prism Launcher](https://prismlauncher.org) (GPL-3.0) with full
  Prism feature set: Minecraft, Modrinth/CurseForge, instance management.
- **Blamdom section** in the launcher: fetches our packs straight from
  mine.blamdom.dev (replaces the FTB section).
- Our own Microsoft app registration for Mojang/MSA login
  (Mojang-enforcement approved).
- Self-updater points at `blamdom/blamdom-launcher` — updates come from us.
- Login success page points at mine.blamdom.dev.
- Java downloader enabled on Linux.
- First self-contained Linux x86_64 build (CI, Qt 6.9.3).

### Changed

- Full de-Prism: name, icons, desktop file, and our own data directory
  (`~/.local/share/BlamdomLauncher`) so it never shares settings with a
  real Prism install.
