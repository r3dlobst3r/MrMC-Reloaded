<h1 align="center">MrMC Reloaded</h1>

<p align="center">
  <strong>A media center for the 10-foot UI — rebased onto Kodi 21.3-Omega</strong>
</p>

<p align="center">
  <a href="LICENSE.md"><img alt="License" src="https://img.shields.io/badge/license-GPLv2-blue.svg?style=flat-square"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS%20%7C%20iOS%20%7C%20tvOS-blue.svg?style=flat-square">
  <img alt="Base" src="https://img.shields.io/badge/base-Kodi%2021.3--Omega-green.svg?style=flat-square">
</p>

---

## About

MrMC is a software media center for playing videos, music, and pictures. It
features a 10-foot user interface for use with televisions and remote
controls, and plays most videos, music, podcasts, and other digital media
from local and network storage and the internet.

**MrMC Reloaded** is a fork of [MrMC](https://github.com/mrmc/mrmc) rebased
onto [Kodi 21.3-Omega](https://github.com/xbmc/xbmc), targeting current
Apple platforms (macOS, iOS, tvOS) and modern Xcode toolchains. The
MrMC-specific feature set is being re-applied onto the modern Kodi core:

- Built-in **Emby**, **Plex**, **Trakt**, and **Philips Hue** services
- The **Ariana** desktop/tvOS skin
- tvOS **Top Shelf** integration
- App Store distribution model (full + Lite with in-app unlock)

## Branches

| Branch | Description |
| --- | --- |
| `omega-rebase` | Kodi 21.3-Omega core + re-applied MrMC features (active development) |
| `master` | Historical MrMC (Kodi 17-era tree, archived) |
| `mrmc-release_*` | Historical MrMC release branches |

## Status

- Base Kodi 21.3-Omega builds on Xcode 27 for macOS, iOS, and tvOS
- MrMC identity (bundle IDs, branding) and the Ariana skin applied
- All built-in services ported to the Kodi 21 core APIs

## Building

See the build guides in `docs/` (`README.macOS.md`, `README.iOS.md`,
`README.tvOS.md`). Dependencies are built via `tools/depends`; the Xcode
project is generated with:

```
make -C tools/depends/target/cmakebuildsys BUILD_DIR=$HOME/mrmc-build GEN=Xcode
```

Note: the Python-binding codegen requires `JAVA_HOME` to point at a JDK
17–21 (the bundled Groovy/ASM does not support JDK 26).

## License

MrMC is GPL licensed — see [LICENSE.md](LICENSE.md). MrMC is a fork of Kodi;
Kodi is a trademark of the XBMC Foundation. This project is not affiliated
with or endorsed by Team Kodi or the original MrMC project.
