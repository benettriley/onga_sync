# ONGA Sync

One installer for the whole ONGA plug-in suite. This repo holds no plug-in code. CI checks
out each plug-in repo at a pinned tag, builds universal (Apple Silicon + Intel) bundles,
and assembles one macOS installer. Tagged builds are published to GitHub Releases, so no
binaries are committed here.

| Plug-in | Repo | Formats |
|---|---|---|
| bloom | `benettriley/onga_bloom` | AU, VST3 |
| transformer | `benettriley/onga_transformer` | AU, VST3, app |
| voxmaster | `benettriley/voxmaster` | AU, VST3, app |
| mageq | `benettriley/mageq_onga` | AU, VST3, app |
| panna | `benettriley/panna` | AU, VST3, app |

Hosts list every plug-in under the maker **ONGA**.

## What the installer does

`ONGA-Sync-<version>.dmg` holds:

- **Install ONGA Sync.pkg**. One checkbox per plug-in, all ticked by default. Each
  installs the Audio Unit to `/Library/Audio/Plug-Ins/Components` and the VST3 to
  `/Library/Audio/Plug-Ins/VST3`. One more checkbox, **Standalone apps**, is unticked
  by default and puts the apps in `/Applications`.
  - Before each part lands, `preinstall` moves copies under earlier names (ONGA BLOOM,
    Voxmaster, magEQ, PANNA, PANNAVISIO…) to the Trash, so hosts list each plug-in once.
    It matches exact names, because APFS ignores case. The plug-in codes never change,
    so saved sessions still open.
  - `postinstall` clears the download quarantine and refreshes the Audio Unit cache.
- **Uninstall ONGA Sync.pkg**. Moves every ONGA bundle, under current and old names,
  to the Trash and forgets the installer receipts.
- **READ ME.txt**.

The welcome and conclusion pages use the ONGA look: Space Mono (falling back to Menlo),
warm grey `#C2BDB1`, ink `#141413` and the red accent `#D9432B`.

## Releasing

Everything the suite ships is listed in [`suite.sh`](suite.sh).

1. Tag the plug-in repo, e.g. `git tag v0.1.1 && git push origin v0.1.1` in `onga_bloom`.
2. In `suite.sh`, set that plug-in's `TAG`, and bump `SUITE_VERSION`.
3. Push, and check the build on the **Actions** tab.
4. Tag this repo `v<SUITE_VERSION>` (e.g. `v2026.1`) and push the tag. CI publishes the
   release with the DMG and the bare pkg attached.

To try branches before tagging, run the workflow by hand with **ref_override** set to a
branch that exists in every plug-in repo.

## CI

`.github/workflows/build.yml`:

1. `plan` reads the plug-in list from `suite.sh`.
2. `build` builds each plug-in on `macos-14` (`scripts/build_plugin.sh`) and checks that
   every binary is universal.
3. `assemble` builds the pkgs and DMG (`scripts/assemble.sh`), installs them on the
   runner with the apps ticked, runs `codesign --verify` and `auval` on everything, runs
   the uninstaller and checks that nothing is left behind.

**Required secret:** `ONGA_REPOS_TOKEN`. The plug-in repos are private, so this needs to
be a fine-grained personal access token with *Contents: read* on the five plug-in repos.

## Signing and notarisation (off for now)

Everything is ad-hoc signed until there's an Apple Developer ID. Ad-hoc signing is enough
for Apple Silicon hosts to load the plug-ins, but users have to right-click the pkg and
choose **Open** the first time. To switch signing on, add these secrets. Nothing else
changes:

| Secret | What |
|---|---|
| `MACOS_CERTS_P12`, `MACOS_CERTS_PASSWORD` | Base64 `.p12` holding the *Developer ID Application* and *Developer ID Installer* certificates, and its password |
| `APP_SIGN_IDENTITY` | `Developer ID Application: Name (TEAMID)`: signs the bundles and DMG, with hardened runtime |
| `INSTALLER_SIGN_IDENTITY` | `Developer ID Installer: Name (TEAMID)`: signs the pkgs |
| `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD` | Turn on notarisation and stapling (app-specific password) |

## Building locally (macOS)

```sh
git clone --recursive https://github.com/benettriley/onga_bloom src/bloom   # etc.
./scripts/build_plugin.sh bloom src/bloom bundles
# …one build_plugin.sh per plug-in…
./scripts/assemble.sh bundles dist
```

## Later: v2

The planned v2 of "Sync" is an app that downloads and updates the plug-ins. It can read
the same `suite.sh` data, published as JSON with each release.
