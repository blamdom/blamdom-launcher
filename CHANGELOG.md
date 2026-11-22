# Changelog

All user-facing changes to the Blamdom Launcher.

**Release rule:** every version gets an entry here *before* its tag is pushed —
CI copies the matching `## <version>` section into the GitHub release notes,
so a release without real notes means an entry was skipped.

## 0.1.13 — 2026-11-22

### Added

- **Update your pack straight from the launcher window.** Right-click any
  instance → **Blamdom pack → Update pack from Blamdom…**: the launcher
  checks mine.blamdom.dev, tells you the current and new version, and
  updates in place with one click — saves and settings are kept, unchanged
  files are skipped, files the new pack dropped are removed. It also says
  clearly when the instance is up to date or isn't one of our packs.

### Fixed

- **Installed packs are now recognized even when the launcher couldn't tell
  what version you have.** Pack detection now falls back through the
  instance's own settings and then its display name, so old installs (and
  packs installed before the launcher tracked the version) still get the
  "Update to v…" button in the Blamdom section instead of being treated as
  "not installed yet". When the version is unknown, the status row says so
  and the update still works.

## 0.1.12 — 2026-11-22

### Fixed

- **Blamdom section now shows fresh releases without a restart.** The pack
  and mod manifests were only fetched once per launcher session (when the
  Blamdom section was first opened), so a release published while the
  launcher was running stayed invisible — the section kept showing the old
  pack/mod versions forever. The section now re-fetches every time it is
  opened, matching the documented "fetched fresh on every open" contract.

## 0.1.11 — 2026-11-22

### Added

- **Our mods are now first-class citizens.** Mods we publish on our site
  (Cozy Quests, Cozy Fixes, …) are recognized automatically: fresh pack
  imports tag them with the **Blamdom** provider in the mod list instead of
  "Unknown", existing installs get the same when the Blamdom section is
  opened, and the launcher never asks you to "choose a mod provider" for
  them — the provider dialog stays for genuinely unknown mods only.
- **In-place updates for our mods.** When a new version of one of our mods is
  posted to the site, the Blamdom section shows it ("Update N mods to latest")
  and updates it in place — the jar is verified against the sha256 in our
  catalog before it replaces the old one, and a crash mid-update can never
  lose your current jar. You can also update a single mod from its
  "Change version" button in the mod list.

### Fixed

- **Self-update archive extraction.** Launcher self-updates (v0.1.10) rejected
  update archives made with common zip tools (Info-ZIP, 7-Zip, …) because of
  a directory entry such tools write for the top-level folder. Those archives
  now extract correctly; the update check itself was unaffected.

## 0.1.10 — 2026-11-21

### Added

- **Windows self-updates.** Windows now updates the launcher the same way
  Linux does: the new version is downloaded, its checksum is verified, and the
  launcher restarts into the new version — no more downloading a zip from the
  site by hand. Your shortcuts and the folder you installed to keep working,
  and your instances, accounts and settings are untouched.

### Changed

- The Windows download is now named `blamdom-launcher-windows-x86_64.zip`
  (same content as before — the old name said `x64`, which didn't match the
  Linux naming or the updater's asset lookup, so Windows update checks failed
  silently and fell back to the download page).

## 0.1.9 — 2026-11-21

### Added

- **Auto-trust for our own site.** Importing a modpack whose mods all come
  from Modrinth or from mine.blamdom.dev no longer shows the "Easy There!"
  untrusted-mods prompt — installing Our Cozy Pack (and our other packs) is
  now one click. Any mod hosted elsewhere still prompts, exactly as before.

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
