# hex-magical

[![Release](https://img.shields.io/badge/release-v0.0.5-blue)](https://github.com/nicapotato/hex-magical/releases/tag/v0.0.5)

## hex-magical v0.0.5

Build status and downloads for the pinned version in [`project.conf`](project.conf). Update this section when bumping `VERSION`.

| Platform | Build | Download |
|----------|-------|----------|
| Web (WASM) | ![Web WASM](https://img.shields.io/badge/web%20wasm-passing-brightgreen) | [hex-magical_wasm.zip](https://github.com/nicapotato/hex-magical/releases/download/v0.0.5/hex-magical_wasm.zip) |
| macOS | ![macOS](https://img.shields.io/badge/macos-passing-brightgreen) | [hex-magical-macos.zip](https://github.com/nicapotato/hex-magical/releases/download/v0.0.5/hex-magical-macos.zip) |
| Windows x64 | ![Windows x64](https://img.shields.io/badge/windows%20x64-passing-brightgreen) | [hex-magical-windows.zip](https://github.com/nicapotato/hex-magical/releases/download/v0.0.5/hex-magical-windows.zip) |

A Crayon Physics Deluxe–style drawing puzzle in C + raylib 6.0 + Box2D 3.1.

Draw rigid crayon shapes with the mouse, then guide the red ball to the star.

## Controls

- **LMB drag** — draw a shape (stays frozen until you drop the ball)
- **RMB** — erase a drawn shape under the cursor
- **START / Enter** — drop the ball and enable gravity
- **R** — restart current level (back to build phase)
- **1 / 2 / 3** — jump to level
- **Space / click** — enter from title, or advance after a win
- **D / DEBUG** — toggle physics collider overlay

## Levels (hard-coded)

1. **Gap** — bridge two plateaus
2. **Climb** — ramp the ball up to a high ledge
3. **Pit** — fling the ball out of a well onto a shelf

## Build

### Desktop (macOS)

```bash
make build      # cmake + Box2D FetchContent + link
make run-mac
```

### Web (Emscripten)

```bash
make build-wasm   # vendors raylib + box2d into external/, emits hex-magical.zip
make run-wasm     # serves at http://localhost:8000/
```

Requires `emcc` on PATH. First web build clones raylib 6.0 and Box2D v3.1.1 under `external/`.

## CI/CD

Unified workflow: [`.github/workflows/hex-magical-cicd.yml`](.github/workflows/hex-magical-cicd.yml)

| Trigger | Behavior |
|---------|----------|
| `make release` | Dispatch all platforms → itch + S3 + **GitHub Release** (creates `v*` tag from `project.conf`) |
| `workflow_dispatch` | Build web/macos/windows (or one platform); push itch + S3 |
| Push tag `v*` | Same + GitHub Release |

```bash
# bump VERSION in project.conf first, commit, then:
make release              # or: make release REF=main
make release-watch        # dispatch + stream the run
make ci PLATFORM=web      # itch/S3 only, no tag
```

Requires [`gh`](https://cli.github.com/) authenticated (`gh auth login`).

**itch.io:** `nicapotato/hex-magical` channels `web` / `osx` / `windows`  
**S3:** `games/prototype/hex-magical/<version>/` (+ catalog merge)

Secrets (same as chlorostitch):

- `BUTLER_API_KEY`
- `AWS_PUBLIC_SOFTWARE_ROLE_ARN`
- `S3_PUBLIC_SOFTWARE_BUCKET` (optional; defaults to `prod-nicapotato-public-software`)

## Project layout

```
src/
  main.c       — entry + window
  platform.c   — desktop vs emscripten main loop
  game.c       — title / playing / win state machine
  physics.c    — Box2D world, ball, drawn bodies
  sketch.c     — stroke capture + RDP simplify
  render.c     — crayon-on-paper draw + debug overlay
  levels.h     — 3 hard-coded LevelDefs
```

## License

zlib/libpng (see LICENSE). Box2D is MIT. raylib is zlib/libpng.
