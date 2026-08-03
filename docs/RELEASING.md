# Releasing

Short enough to follow without thinking, which is the point: releases here
are infrequent enough to forget the order.

## Versioning

[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The public
interface of a desktop mail client is not an API; for this project it means:

- the configuration file format (section names, key names, their meaning)
- the keybinding action names bindable from `[keys]`
- the command-line interface
- where configuration is read from

Below 1.0.0 those may change in a minor release. 1.0.0 is the point at which
they stop changing under users, which is a decision to make deliberately
rather than a milestone that arrives on its own.

- **MAJOR**: a config file that worked before now does not, or an action
  name is removed or changes meaning.
- **MINOR**: a feature, a new action name, a new config key that older
  configs simply do not set.
- **PATCH**: a fix that leaves all of the above alone.

## Steps

1. **Confirm the tree is clean and the tests pass.**

   ```bash
   git status --short
   cmake --build build && ctest --test-dir build --output-on-failure
   ```

2. **Bump the version.** It is declared in exactly one place, the
   `project()` call in the top-level `CMakeLists.txt`; `src/version.h.in`
   generates `version.h` from it. Do not write the number anywhere else.

   ```cmake
   project(qtmaildir VERSION 0.2.0 LANGUAGES CXX)
   ```

3. **Move `Unreleased` in `CHANGELOG.md`** to a new dated section, and open
   an empty `Unreleased` above it.

   ```markdown
   ## [Unreleased]

   Nothing yet.

   ## [0.2.0] - 2026-09-01
   ```

4. **Rebuild and check the version actually changed.** The generated header
   is a build artifact, so a stale build directory will happily report the
   old number.

   ```bash
   cmake -S . -B build && cmake --build build
   ./build/src/qtmaildir --version
   ```

5. **Commit and tag.** Tags are annotated and signed, like every commit in
   this repository.

   ```bash
   git add CMakeLists.txt CHANGELOG.md
   git commit -S -m "release: 0.2.0"
   git tag -s v0.2.0 -m "qtmaildir 0.2.0"
   ```

6. **Verify the tag is signed**, then push if there is a remote.

   ```bash
   git tag -v v0.2.0
   git push && git push --tags
   ```

## After a release that changes the config format

Note it in the changelog under `Changed` with the old and new spelling side
by side. A user whose config silently stops working will not go looking for
a version number to blame.
