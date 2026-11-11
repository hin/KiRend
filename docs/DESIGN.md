# KiRend design

KiRend is a command-line tool that reads a KiCad 10 `.kicad_pcb` file and renders the board and its 3D models as black line art on a white background (IKEA-instruction style), written as SVG or PNG. It builds on Windows, Linux and macOS.

## 1. Decisions

| Topic | Decision |
|---|---|
| Language / build | C++20, CMake, dependencies through vcpkg (manifest mode, pinned baseline) |
| KiCad versions | KiCad 10 file format and later. Older files are read with a warning, not rejected |
| Style | Black lines on white, hidden lines removed, outlines/silhouettes drawn thicker than inner edges |
| Style sheets | YAML file given with `--style`: line widths (points) and colours per line category, background, what is drawn. Built-in default style; a file only lists what it changes. Appearance only, so one style works for any board and view. Command line options win over it. Errors in the file are fatal |
| Views | `--side`, `--rotate`, `--zoom`, `--pan`, `--pivot`, `--perspective` with kicad-cli semantics |
| Framing | Pixel-exact with `kicad-cli pcb render`, so a KiCad render and a KiRend image overlay |
| Board content | Board body (Edge.Cuts) plus pads, tracks, vias and graphics per layer via `--layers` |
| Silkscreen text | Not drawn yet |
| Models | STEP and WRL, embedded or separate files; STEP wins when both exist; missing models are skipped with a warning |
| Path resolution | Same rules and precedence as KiCad / kicad-cli |
| CLI | `kirend [options] INPUT_FILE`, kicad-cli option names; options that don't apply to line art are not implemented |
| PNG | Raster of the same drawing, 300 DPI stored in the file |
| Later | Callouts, highlighting, hiding parts, exploded views; style settings for line caps/joins, dashes and crease angle |
| Libraries | OCCT 8.0, zstd, nlohmann/json, p-ranav/argparse, lunasvg (+ plutovg), yaml-cpp, GoogleTest (tests only) |

## 2. Command line

```
kirend [options] INPUT_FILE
```

| Option | Meaning | kicad-cli |
|---|---|---|
| `-o, --output FILE` | Output file, `.svg` or `.png`. Default `<board>-<side number>.png`, like kicad-cli | same |
| `-D, --define-var KEY=VALUE` | Override or add a variable (repeatable) | same |
| `-w, --width N` / `-h, --height N` | Window size in pixels (default 1600 x 900) | same |
| `--side top\|bottom\|left\|right\|front\|back` | Base view (default `top`) | same |
| `--rotate "X,Y,Z"` | Extra rotation in degrees, e.g. `'-45,0,45'` for isometric | same |
| `--zoom F` | Zoom factor; KiCad clamps it to 0.5 ... 50 | same |
| `--pan "X,Y,Z"` | Camera pan in KiCad 3D-viewer units | same |
| `--pivot "X,Y,Z"` | Pivot relative to the board centre, in cm | same |
| `--perspective` | Perspective instead of orthographic | same |
| `--background default\|transparent\|opaque` | `default` uses the style's background (white); `opaque` uses it too, or white if the style's is transparent | default differs (kicad-cli: transparent PNG) |
| `--style FILE` | YAML style sheet, see section 3.8 | new |
| `-l, --layers L1,L2,...` | Layers to draw, overrides the style's `visible.layers`. `Edge.Cuts` = board body with holes; copper layers = pads, tracks, vias; other layers = their graphics. User names like `F.Silkscreen` work too. Default from the style: `Edge.Cuts` | new (same style as `kicad-cli pcb export`) |
| `--dpi N` | DPI stored in the PNG (default 300); also converts the style's line widths from points to pixels | new |
| `-v, --verbose` | Timings and resolved model paths | new |
| `--help`, `--version` | `-h` is the height, as in kicad-cli | same |

Not implemented because they have no meaning for line art: `--quality`, `--preset`, `--use-board-stackup-colors`, `--floor`, `--light-*`. Not implemented yet: `--variant`.

Exit codes are kicad-cli's: 0 OK, 1 bad arguments, 3 unreadable input, 4 output problem.

## 3. Pipeline

```
.kicad_pcb ─► S-expression parser ─► Board model ─► outline, holes, layer z heights
                                         │
                       path resolver ◄── (model ...) entries
                             │
       STEP (OCCT) / WRL (OCCT) / embedded (base64 + zstd)
                             │
        meshes with feature edges, placed like KiCad's 3D viewer
                             │
                camera (kicad-cli compatible)
                             │
     feature + silhouette edges ─► depth-buffer visibility ─► polylines
                             │
                   SVG writer ─► lunasvg ─► PNG (+ pHYs DPI chunk)
```

### 3.1 Board file (src/pcb, src/sexpr)

A hand-written S-expression parser (including `|...|` data blocks) feeds a reader that keeps what KiRend draws: stackup thicknesses, layer names, Edge.Cuts and other graphics, footprints (position, rotation, side, attributes, pads, models, embedded files), tracks, arcs, vias and board embedded files. Footprint graphics and pads are converted to board coordinates when read. Unknown items are ignored.

### 3.2 Board geometry (src/geometry)

- Edge.Cuts shapes are chained into closed contours (0.01 mm tolerance, as KiCad); nesting decides outline vs cut-out. Without a usable outline, the bounding box is used, as KiCad does.
- The board body is the outline minus cut-outs and all plated/non-plated drill holes and vias. OCCT triangulates the top face; the bottom face and walls are built from it. Holes crossing the board edge (castellations) or overlapping each other are removed with an OCCT boolean cut.
- Layer heights follow `BOARD_ADAPTER::InitSettings()`: body thickness from the stackup (or KiCad's default stackup), copper on top of it, silkscreen above, and footprints placed at the top of F.Paste (front) or the bottom copper (back).

### 3.3 Model paths (src/paths)

Mirrors `FILENAME_RESOLVER::ResolvePath()` and `KIwxExpandEnvVars()`:

1. `kicad-embed://name` → footprint embedded files, then board embedded files.
2. `${VAR}` / `$(VAR)` lookup order: `-D` values and project text variables (`.kicad_pro`), `KIPRJMOD`, process environment, `environment.vars` from `kicad_common.json`, the built-in stock path for `KICAD10_3DMODEL_DIR`. Other versioned names (`KICAD8_3DMODEL_DIR`, `KISYS3DMOD`) fall back to the current one.
3. The file is tried as given, relative to the project directory, then relative to `KICAD10_3DMODEL_DIR`.
4. **STEP wins**: a resolved `.wrl` with a `.step`/`.stp` next to it (or embedded with the same name) is replaced by the STEP file.
5. A `.wrl` that can't be found is replaced by a STEP file with the same name from the search paths (project folder, KiCad path variables, `<project>/3dshapes`, `system.extra_3d_search_dirs`), preferring the same `.3dshapes` folder. kicad-cli 10 does this because the KiCad 10 libraries no longer contain WRL files.

`kicad_common.json` is read from the newest KiCad 10+ settings folder (`~/Library/Preferences/kicad/10.0` on macOS, `~/.config/kicad/10.0` on Linux, `%APPDATA%\kicad\10.0` on Windows, or `KICAD_CONFIG_HOME`). Stock models default to `/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels`, `/usr/share/kicad/3dmodels` or `C:\Program Files\KiCad\10.0\share\kicad\3dmodels` (or `KICAD_STOCK_DATA_HOME/3dmodels`).

Not implemented yet: resolving relative model paths against the footprint library folder from `fp-lib-table`, and legacy `:alias:` paths.

Footprints marked DNP are hidden, like the default of KiCad's 3D viewer. Hidden models and models with opacity 0 are skipped.

### 3.4 Models (src/models)

- STEP: OCCT `STEPControl_Reader`, tessellated with `BRepMesh_IncrementalMesh` (linear deflection about a quarter of an output pixel). Sharp B-rep edges become feature lines; edges between tangent faces and seam edges don't.
- WRL: OCCT `VrmlData_Scene`, scaled from 0.1 inch to mm. Feature lines come from crease detection (faces meeting at more than 30°).
- Placement uses the exact matrix chain of `RENDER_3D_RAYTRACE_BASE::load3DModels()`. Models are cached per file.

### 3.5 Camera (src/render/camera.cpp)

Reproduces `PCBNEW_JOBS_HANDLER::JobExportRender()` with `TRACK_BALL`/`CAMERA`:

- Scale: the larger side of the board bounding box (Edge.Cuts, including stroke width) maps to 12.8 3D units; the look-at point is the bounding box centre plus `--pivot`.
- Camera at distance 16, 45° vertical field of view; orthographic views keep the same visible size. `--zoom` is clamped like KiCad (camera zoom 0.02 ... 2).
- Rotations: side preset plus `--rotate`, applied as X, then Y, then Z.
- Output size: kicad-cli's ray tracer renders in blocks and writes an image slightly smaller than `-w x -h` (1568 x 872 for 1600 x 900), centred in the window. KiRend writes exactly the same size and offset. In perspective mode the ray tracer's 0.075 px sample offset is included.

### 3.6 Hidden-line rendering (src/render/line_renderer.cpp)

1. Each mesh is welded and its edges get adjacency.
2. Candidate lines: feature edges, open boundary edges, and silhouette edges (both neighbouring triangles on the same side of the edge on screen; independent of triangle winding, so badly oriented WRL files work).
3. All triangles go into a depth buffer at 3x the output resolution.
4. Each candidate edge is sampled every half sub-pixel; a sample is visible if any neighbouring depth sample is not more than 5 µm in front of it. Board layer lines (pads, tracks, graphics) are tested the same way but don't occlude anything.
5. Visible pieces are chained into polylines and simplified (0.05 px).

Every line carries a category (board, models, pads, tracks, vias, graphics) and, for the board body and models, whether it is an outline (silhouette or open boundary) or an inner edge. The style sheet gives each its width and colour.

### 3.7 Output (src/output)

- SVG: one group of `<path>` elements per line category with round caps and joins: `board-edges`, `models-edges`, `pads`, `tracks`, `vias`, `graphics`, then `board-outlines` and `models-outlines` on top. Empty groups are left out. A background rectangle unless the background is transparent.
- PNG: the same SVG rendered by lunasvg at the output size, with a `pHYs` chunk for the DPI.

### 3.8 Style sheets (src/style, styles/default.yaml)

A style sheet is a YAML file (read with yaml-cpp) passed with `--style FILE`. The built-in default style is `styles/default.yaml`, compiled into the program; it documents every setting and is the example to copy from. A user's file is applied on top of the default, so it only needs the settings it changes.

```yaml
background: "#ffffff"        # or transparent
visible:
  layers: [Edge.Cuts]        # --layers overrides this
  models: true
  dnp: false                 # models of DNP footprints
  pads: true
  tracks: true
  vias: true
  graphics: true
lines:
  board:  { outline: { width: 0.70pt, color: "#000000" }, edge: { width: 0.35pt, color: "#000000" } }
  models: { outline: { ... }, edge: { ... } }
  pads:     { width: 0.35pt, color: "#000000" }
  tracks:   { ... }
  vias:     { ... }
  graphics: { ... }
```

- Widths are in points on the printed image and are converted to pixels with `--dpi` (px = pt × DPI / 72), so a line has the same thickness in a document whatever the image size. The defaults (0.35 and 0.70 pt) are 1.46 and 2.92 px at 300 DPI, the widths KiRend used before style sheets at the default 1600 x 900 window. Before style sheets, widths scaled with the image height instead, so other sizes now get slightly different line widths.
- Colours are `"#rrggbb"` or `"#rgb"`, in quotes (an unquoted `#` starts a YAML comment; KiRend reports that case).
- Unknown settings, wrong types and bad values are errors (exit code 1) with file, line and column.
- The style only controls appearance. View and image size stay on the command line, and command line options (`--layers`, `--background`) win over the style.

## 4. Known limitations

- Silkscreen and other text is not drawn.
- Copper zones (pours) are not drawn; overlapping pads and tracks are drawn as separate outlines, not merged.
- Padstacks with different shapes per layer use the default shape.
- `--variant`, fp-lib-table relative model paths and `.stpz` / `.step.gz` / IGES models are not supported.

## 5. Project layout

```
CMakeLists.txt, CMakePresets.json, vcpkg.json
src/main.cpp, src/kirend.cpp       entry point and render job
src/cli/        command line (argparse)
src/sexpr/      S-expression parser
src/pcb/        board model and reader
src/geometry/   vectors, arcs, outline, board body, layer lines
src/paths/      variables, KiCad settings, model resolver, base64, embedded files
src/models/     STEP/VRML loading and placement
src/render/     meshes, camera, hidden-line renderer
src/output/     SVG and PNG writers
src/style/      style sheets (YAML)
styles/         built-in default style sheet
tests/          GoogleTest unit tests and small test boards
```

## 6. Status

| Milestone | State |
|---|---|
| CMake + vcpkg skeleton, CLI | done |
| Board parser, outline, board body | done |
| Camera matching kicad-cli | done; checked against kicad-cli 10.0 (Docker image `kicad/kicad:10.0`) for top, bottom, front, isometric, perspective, zoom/pan, pivot and 800 x 600 views, with and without 3D models: same image size, no pixel offset, mean line-to-edge distance under 0.7 px |
| STEP/WRL models, path resolution | done |
| Embedded models | done |
| `--layers` | done (see limitations) |
| PNG with DPI | done |
| Style sheets (`--style`) | done |
