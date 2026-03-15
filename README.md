# Docksmith

Docksmith is a simplified Docker-like build and runtime system implemented as a single CLI binary.

## What this project implements
- `docksmith build -t <name:tag> [--no-cache] <context>`
- `docksmith images`
- `docksmith rmi <name:tag>`
- `docksmith run [-e KEY=VALUE ...] <name:tag> [cmd ...]`

Build language implemented from `Docksmithfile`:
- `FROM`
- `COPY` (supports `*` and `**` globs)
- `RUN`
- `WORKDIR`
- `ENV`
- `CMD` (JSON array form)

## Code structure
- `src/main.cpp`
  - CLI entry point and command routing.
- `src/build_engine.cpp`
  - Build pipeline, instruction execution, cache hit/miss logic, manifest creation.
- `src/dockerfile_parser.cpp`
  - Strict parser for the six supported instructions with line-numbered errors.
- `src/cache_engine.cpp`
  - Deterministic cache key generation.
- `src/layer_engine.cpp`
  - Filesystem snapshots, delta detection, deterministic tar layer creation, tar extraction.
- `src/runtime_engine.cpp`
  - Container run flow and Linux isolation primitive used by both `RUN` and `docksmith run`.
- `src/state_store.cpp`
  - `~/.docksmith` state management: image manifests, layer paths, cache index.
- `src/utils.cpp`
  - SHA-256 implementation, path/string helpers, timestamp utility.
- `include/*.h`
  - Public interfaces and shared types.

## State layout
Docksmith stores local state in `~/.docksmith/`:
- `images/` manifest JSON files
- `layers/` content-addressed tar files (`<sha256>.tar`)
- `cache/index.json` cache key -> layer digest mapping

## Build behavior summary
1. Parse `Docksmithfile` from the provided context.
2. Resolve `FROM` base image from local store only.
3. Assemble working rootfs by extracting base layers.
4. For each `COPY`/`RUN`:
   - Compute cache key from previous layer/base digest + instruction text + current `WORKDIR` + current `ENV` + COPY source hashes.
   - On hit: reuse cached layer and extract it.
   - On miss: execute step, compute delta, create deterministic tar, hash/store layer, update cache.
   - Cascade rule: first miss forces all later layer-producing steps to miss.
5. Write final manifest with digest computed from canonical JSON where `digest` is empty during hash computation.

## Runtime behavior summary
- `docksmith run` assembles rootfs from image layers in order.
- Applies image `ENV`, then `-e` overrides.
- Uses image `WorkingDir` (or `/` default).
- Runs image `CMD` unless command override is provided.
- Uses the same Linux isolation function as build-time `RUN`.

## Linux isolation note
- Process isolation is implemented using Linux `chroot` + `fork/exec`.
- This requires Linux and privileges capable of `chroot` (typically root/CAP_SYS_CHROOT).
- If you are on Windows/macOS, run inside a Linux VM as required by the project spec.

## Base image preload (offline requirement)
No network pull is done by Docksmith during build/run. You must preload base image manifests and layer tar files into `~/.docksmith` before building.

Expected format:
- Manifest in `~/.docksmith/images/<name>_<tag>.json`
- Layer files in `~/.docksmith/layers/<digest-without-sha256-prefix>.tar`

## Sample app
A minimal sample app is included in `sample-app/`.

Run demo sequence:
1. `docksmith build -t myapp:latest sample-app`
2. `docksmith build -t myapp:latest sample-app`
3. Edit `sample-app/message.txt` then rebuild.
4. `docksmith images`
5. `docksmith run myapp:latest`
6. `docksmith run -e GREETING=NewValue myapp:latest`
7. Validate host isolation.
8. `docksmith rmi myapp:latest`

See `docs/VALIDATION.md` for expected outcomes.
