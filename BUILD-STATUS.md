# Platform verification

Sources: version 1.4.0, based on the supplied m1zuki.zip plus the Windows and platform updates.

The `Linux and macOS packages` workflow builds and tests the actual sources on Arch Linux and macOS, then packages the resulting binaries. Only a successful run containing downloadable artifacts constitutes a verified build. See the Actions run for the exact commit and results.

Headless tests cover parsing, settings, shortcut validation/rollback, overlay behavior and platform capability checks. They do not verify a real KDE Wayland desktop, macOS screen recording permissions, multiple physical monitors or exclusion from third-party recorders. These require an interactive test on the target system.

macOS cannot provide the Windows/KDE capture-exclusion behavior. Showing answers requires explicit opt-in to an unprotected overlay. The app is ad-hoc signed, not notarized with an Apple Developer ID.
