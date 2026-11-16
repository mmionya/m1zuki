# Platform verification

Sources: version 1.4.0, based on the supplied m1zuki.zip plus the Windows and platform updates.

## Verified on 2026-11-16

Native Linux/macOS build commit: `2775565ea979029af0af3cd7a02e55f5bea7b611`.
Successful run: https://github.com/mmionya/m1zuki/actions/runs/43854845439
Build branch: `codex/platform-builds-20261116`. The v1.4.0 release promotes these sources to main and preserves the verified Linux/macOS binaries; Windows is rebuilt and tested during publication.

| Platform | Result |
| --- | --- |
| Arch Linux x86_64 | Release build; all 5 CTest groups passed; generated KWin script regression passed; shell syntax passed; tar.gz produced. |
| macOS 15 ARM runner | Universal arm64+x86_64 build targeting macOS 13+; all 6 CTest groups passed with no skipped cases, including native Cocoa; bundled app launched with development Qt paths removed; screenshot of settings reviewed. |
| Windows x64, local | Release build; all 5 CTest groups passed. Shortcut tests require the running app to be closed temporarily; it was reopened afterward. |

Mac bundle architecture and ad-hoc signature were verified on the Mac runner. The downloaded Mac/Linux GitHub artifact SHA-256 digests match. Local ZIP integrity, Mac bundle metadata/frameworks and Linux ELF architecture/executable mode were checked. The Intel slice was compiled and inspected, not executed on an Intel Mac.

Artifacts are in `dist/`: `m1zuki-linux-x86_64.tar.gz`, `m1zuki-macos-arm64+x86_64.zip`, and `m1zuki-1.4.0-source.zip`. `SHA256SUMS.txt` records their local checksums.

The `Linux and macOS packages` workflow builds and tests the actual sources on Arch Linux and macOS, then packages the resulting binaries. Only a successful run containing downloadable artifacts constitutes a verified build. See the Actions run for the exact commit and results.

Headless tests cover parsing, settings, shortcut validation/rollback, overlay behavior and platform capability checks. They do not verify a real KDE Wayland desktop, macOS screen recording permissions, multiple physical monitors or exclusion from third-party recorders. These require an interactive test on the target system.

macOS cannot provide the Windows/KDE capture-exclusion behavior. Showing answers requires explicit opt-in to an unprotected overlay. The app is ad-hoc signed, not notarized with an Apple Developer ID.
