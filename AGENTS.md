# Project rules

- Every push bumps `VERSION` in `Main.h` and adds an entry to `CHANGELOG.md`, then pushes a matching `vX.Y.Z` tag so CI publishes the release (CI fails if the tag and `VERSION` differ).
