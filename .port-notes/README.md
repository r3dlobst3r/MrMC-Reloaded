# MrMC → Kodi 21.3-Omega port notes

## Key facts

- **Merge-base** between MrMC master and Kodi 21.3-Omega:
  `183b21380ba6ef570fc2a121c9124314e33b5a00` (2015-08-23, Jarvis-era).
  MrMC's rebrand happened 2015-09-15 (`a290692ec3`); upstream syncs (cherry-picks)
  continued until 2019 (`a29a531dcd` "core: upstream sync" being the last).
- **Caveat on the modified list**: because the merge-base predates Krypton, the
  1,658 "modified" files include upstream Krypton commits MrMC cherry-picked —
  work that Kodi 21.3-Omega already contains. Do NOT treat the modified list as
  MrMC-unique. The additive list is accurate (upstream has no `xbmc/services`, etc.).
- For shared-file touch points, rely on the feature inventory below, not the raw diff.

## Feature inventory (the real porting map)

1. `xbmc/services/` — emby (7 classes), plex, trakt, hue+lighteffects, ServicesManager (~16k lines). Wired into Application.cpp; `emby://` in DirectoryFactory.cpp.
2. `xbmc/windows/GUIWindowHome` + `xbmc/utils/HomeShelfJob` — HomeShelf window; registered in GUIWindowManager.cpp.
3. `xbmc/cores/dvdplayer/DVDCodecs/Video/DVDVideoCodecAVFoundation.{mm,h}` (~1050 lines, hev1/dvhe) + `AudioSinkAVFoundation.{mm,h}` — decide vs Omega's VideoToolbox.
4. Lite/IAP: `xbmc/platform/darwin/RMStore/`, `xbmc/utils/LiteUtils.{cpp,h}` — 17 files reference LiteUtils (Settings.cpp, AdvancedSettings.cpp, GUIMediaWindow.cpp, GUIWindowFileManager.cpp, GUIWindowSplash.cpp, GUIWindowMusicNav.cpp, PVRChannelGroup.cpp, NetworkServices.cpp, Splash.cpp, json-rpc/SystemOperations.cpp, interfaces/legacy/Control.cpp, MainController.mm, XBMCApplication.mm, IOSExternalTouchController.mm, TVOSTopShelf.mm, LiteUtils.{cpp,h}).
5. tvOS: FocusEngineHandler + FocusLayerView (darwin/ root), ProgressThumbNailer, TVOSTopShelf.mm (app-group `group.tv.mrmc.shared`, thumbs in group container `Library/Caches/RA/`), MainKeyboard*, TopShelf/ xpc (ServiceProvider.m reads app-group defaults — 270 lines).
6. iOS: CarPlayDelegate.mm, IOSExternalTouchController, IOSPlayShared, xbmc/utils/Splash.cpp, ios-common/ (AnnounceReceiver, VideoLayerView).
7. Branding: version.txt (APP_PACKAGE tv.mrmc.mrmc), media/ splash+icons, resource.uisounds.mrmc/.tvos/.amber, log.mrmc.tv upload, Standard/Expert-only settings.
8. Drop: tizen platform, libdvd (MrMC removed DVD support), UnrarXLib (check Omega equivalent), windows platform.

## Strategy

- Branch `omega-rebase` off 21.3-Omega; apply MrMC delta feature-by-feature.
- Master branch keeps MrMC-as-was (last: 5a8e460b2a, 2019-11-18).
- Stashed on master: `git stash` "wip: MACOSX_DEPLOYMENT_TARGET 27.0 experiment" (pbxproj only; the MrMC.xcodeproj is retired by this port anyway).

## Manifest files

- `additive-since-mb.txt` — files added by MrMC since merge-base (1,188 lines). Accurate map of new files.
- `modified-since-mb.txt` — superset; includes cherry-picked upstream work (1,658 lines). Use with caution.

## Binary addons (PVR + visualizations)

Built via `tools/depends/target/binary-addons` against the `mrmc` (not `kodi`) header
bindings. The bindings install to `build/include/mrmc/`, so most addons use
`${KODI_INCLUDE_DIR}/..` to reach the `kodi/AddonBase.h` include path; a
`kodi -> mrmc` symlink is created in `build/include/` to satisfy them.

**ADDONS_TO_BUILD** (14 addons; `pvr.tvmosaic` has no Omega definition and is
intentionally dropped):
```
pvr.dvblink pvr.dvbviewer pvr.hdhomerun pvr.hts pvr.iptvsimple
pvr.mediaportal.tvserver pvr.mythtv pvr.nextpvr pvr.stalker pvr.vbox
pvr.vdr.vnsi pvr.vuplus visualization.spectrum visualization.waveform
```

**Build reproducibility gotchas** — these edits are applied to *extracted* addon
sources under `build/<addon>/` and are REVERTED whenever ExternalProject
re-runs download/extract. A clean build needs them re-applied (or converted to
`PATCH_COMMAND` patches):
1. `pvr.mediaportal.tvserver` + `pvr.vuplus` `CMakeLists.txt`: use
   `${TINYXML_INCLUDE_DIRS}` (plural) — their `FindTinyXML.cmake` only defines the
   plural var, not `${TINYXML_INCLUDE_DIR}`.
2. `pvr.vuplus` `CMakeLists.txt`: `${KODI_INCLUDE_DIR}` → `${KODI_INCLUDE_DIR}/..`
   (every other addon already uses `/..`).
3. `visualization.spectrum` + `visualization.waveform` `CMakeLists.txt`:
   `${GLM_INCLUDE_DIR}` → `${GLM_INCLUDE_DIR}/..` (glm's `Findglm.cmake` uses
   `PATH_SUFFIXES glm`, but sources `#include <glm/glm.hpp>`).
4. `Toolchain_binaddons.cmake`: `CMAKE_C_COMPILER ... -std=gnu23` →
   `-std=gnu17` (auto-detected `-std=gnu23` breaks K&R C in mediaportal's bundled
   live555). Persist via `tools/depends/target/Toolchain_binaddons.cmake.in`.
5. glm dependency: append `-Wno-reserved-identifier` to its
   `add_compile_options(-Werror -Weverything ...)` (glm builds with `-Weverything`).
6. zlib tarball: re-download from `mirrors.kodi.tv/build-deps/sources/` if the
   `zlib.net` URL 404s (empty tarball).

Built `.dylib` files land in `addons/<addon-id>/` and are **gitignored**; they are
regenerated at build time by the `binary-addons` target (or
`make -C tools/depends/target/binary-addons ADDONS="..."`). The app bundle picks
them up automatically via `CopyRootFiles-*.command` rsyncing `addons/`.
