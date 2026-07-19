# hex-magical

A Crayon Physics Deluxe–style drawing puzzle in C + raylib 6.0 + Box2D 3.1.

Draw rigid crayon shapes with the mouse, then guide the red ball to the star.

## Controls

- **LMB drag** — draw a shape (stays frozen until you drop the ball)
- **RMB** — erase a drawn shape under the cursor
- **START / Enter** — drop the ball and enable gravity
- **R** — restart current level (back to build phase)
- **1 / 2 / 3** — jump to level
- **Space / click** — enter from title, or advance after a win

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

## Project layout

```
src/
  main.c       — entry + window
  platform.c   — desktop vs emscripten main loop
  game.c       — title / playing / win state machine
  physics.c    — Box2D world, ball, drawn bodies
  sketch.c     — stroke capture + RDP simplify
  render.c     — crayon-on-paper draw
  levels.h     — 3 hard-coded LevelDefs
```

## License

zlib/libpng (see LICENSE). Box2D is MIT. raylib is zlib/libpng.
