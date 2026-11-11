# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

This policy covers the Windows installer of AMVScope (`AMVScope-x.y.z-Windows-Setup.exe`) published on the [Releases](../../releases) page.

## How releases are built

- Every release is built from the source code in this repository by GitHub Actions (`.github/workflows/build.yml`). Nothing is built or changed by hand.
- The release for a version is created from the commit on `main` that raised the version number in `CMakeLists.txt`.
- Only files built from this repository are signed. The plugin uses the [JUCE](https://juce.com) framework, which is compiled from source as part of the build.

## Team roles

- **Committers and reviewers:** [airamvlad](https://github.com/airamvlad)
- **Approvers:** [airamvlad](https://github.com/airamvlad)

Changes from anyone else are reviewed by a committer before they are merged. Every signing request is approved by an approver.

## Privacy policy

This program will not transfer any information to other networked systems.

AMVScope has no network code at all: it only reads the audio the host sends it and draws it. Instances of the plugin running in the same host process share their waveforms with each other in memory; nothing leaves the computer.

## Uninstalling

- **Windows:** *Settings > Apps > Installed apps > AMVScope > Uninstall*, or delete the folder `C:\Program Files\Common Files\VST3\AMVScope.vst3`.
- **macOS:** delete `/Library/Audio/Plug-Ins/VST3/AMVScope.vst3`.
