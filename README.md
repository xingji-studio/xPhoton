# xPhoton

xPhoton is a native XAPI bitmap editor for XJ380. Source and app-specific
resources live in this directory. Each process owns one document; multiple
instances can run independently.

## Build and launch

```sh
python3 tools/gen_ninja.py --out build.ninja
./ninja -f build.ninja out/xphoton.elf out/launcher.elf
./ninja -f build.ninja vdisk
```

The image contains `/apps/builtin/xphoton/xphoton.elf` and
`/system/resources/icon/xphoton.png`. Search for `xPhoton`, `paint`, `draw`, `huitu`,
or `huatu` in the launcher. The app accepts an optional image path using both
normal `argv[1]` and the legacy XAPI single-argument convention. The existing
image viewer remains the default image-opening application.

## Editing

- Pencil, round brush, eraser, contiguous exact-color flood fill and eyedropper.
- Line, rectangle and ellipse with outline, fill, or outline plus fill.
- Primary and secondary colors, 24 swatches, hexadecimal RGB input and color swap.
- Rectangular selection, move, system clipboard copy/cut/paste, delete and crop.
- Canvas size changes (top-left anchored, new area uses the secondary color),
  nearest-neighbor image resizing, 90-degree rotation and horizontal/vertical flips.
- Zoom from 25% to 800%, fit, actual size, scrollbars and canvas panning.
- PNG/BMP/JPEG import through XAPI, PNG and 32-bit RGBA BMP export. BMP uses a
  BITMAPV4HEADER with explicit alpha masks. Saving and re-opening preserves RGBA
  pixels, including transparency. Transparent pixels display over a checkerboard.
- Undo and redo per complete gesture or image operation. The dirty marker follows
  the saved history state, including after undo/redo. New, Open and File > Exit
  ask how to handle unsaved changes. Save As confirms replacement of an existing file.
  New images default to the current user's desktop when available.
- The interface follows the system's Chinese/English language setting at startup.

Left drag draws with the primary color; right drag uses the secondary color.
The eraser paints with the secondary color. Right-clicking a swatch sets the
secondary color. Clicking either current-color chip opens its hexadecimal editor.
Selections move as an opaque rectangular block, preserving the exact pixels
inside them; the original area is cleared to the secondary color.

| Shortcut | Action |
| --- | --- |
| Ctrl+N / O / S | New / Open / Save |
| Ctrl+Shift+S | Save As |
| Ctrl+Z / Y or Ctrl+Shift+Z | Undo / Redo |
| Ctrl+A / X / C / V | Select All / Cut / Copy / Paste |
| Delete / Esc | Delete selection / cancel gesture, menu or selection |
| P / B / E / F / I | Pencil / Brush / Eraser / Fill / Eyedropper |
| L / R / O / S / H | Line / Rectangle / Ellipse / Select / Pan |
| [ / ] / X | Decrease size / increase size / swap colors |
| Shift while drawing a shape | Horizontal/vertical/diagonal line or square/circle |
| Space+drag or middle drag | Pan |
| Ctrl+wheel or Ctrl++ / Ctrl+- | Zoom |
| Wheel / Shift+wheel / arrow keys | Scroll |
| Ctrl+0 / F1 | Fit to window / Help |

## Internal design

`canvas.*` owns RGBA pixels and viewport geometry. `tools.*` implements bounded
rasterization, an iterative scanline fill and image transforms. `history.*`
stores reversible XOR patches for changed regions and swaps whole images for
size changes. It keeps at most 128 actions within 64 MiB. A transaction holds a
temporary original image while a gesture is active; a failed history allocation
cancels that gesture instead of leaving an untracked modification.

Images are limited to 8192 pixels per side and 8,388,608 pixels in total.
Dimension products are checked before allocation. This is an allocation limit;
large operations can still fail if the OS cannot provide enough free memory.

`app.*` serializes edits on the main thread. The XAPI callback in `main.cpp`
only publishes bounded messages. Queue overflow restores an active edit and
clears modifiers. The callback does not wait for the editor while a system file
dialog is open. Legacy CHAR/SPCHAR messages are ignored because KEYDOWN already
carries those physical keys; the numeric/hex dialogs disable IME composition.

`ui.cpp` keeps selections, menus and shape previews out of exported pixels.
The viewport samples the image without changing it and uploads tightly packed
24-row RGBA strips. Brush motion invalidates only its changed display region.
Toolbars wrap with the window; app-owned glyphs use the RGBA rasterizer and
do not require external system SVG icons. The initial window fits the screen
using a dimensions-only query. Ribbon rows that do not fit a very short window
are hidden while the menus remain accessible.

`image_io.*` encodes before changing files, handles partial writes/EINTR, syncs
and closes a sibling temporary file, and preserves the previous file under a
reserved backup name during replacement. XJ380's rename implementation deletes
an existing target before moving the source, so this app does not assume POSIX
atomic replacement. Failed replacement attempts restore the original; if that
rollback also fails, the original backup and new temporary image remain on disk
and the status identifies the backup. These steps do not promise crash-atomic
saving across power loss.

`png_codec.*` compiles the repository's existing, unchanged LodePNG source with
its C++ wrappers, disk API, decoder and ancillary chunks disabled. File I/O uses
XAPI's POSIX descriptors. LodePNG copyright and license are in
`assets/LICENSE.LodePNG` and are staged under `/system/licenses/xphoton/`.

## Current platform limits

Use **File > Exit** to receive the unsaved-change prompt. The native window
frame's close button currently destroys a window and its last task directly;
XAPI has no cancellable close notification. xPhoton does not change that
kernel/window-manager behavior. The system clipboard transfers RGBA selections between xPhoton instances and
the screenshot tool through MSG_PASTE snapshots. Text rendering into the image,
IME text editing and freeform selections are reserved for later work.

## Verification

```sh
python3 -m unittest discover -s tests -p test_xphoton.py -v
```

The host harnesses execute the actual rasterizer, history, viewport, codecs,
save recovery, GUI controller and message queue with AddressSanitizer and
UndefinedBehaviorSanitizer. PNG/BMP exports are decoded by the same stb decoder
used by XJ380 and compared pixel-for-pixel. Fault injection covers allocation
failure, partial/interrupted writes, sync failure and failed replacement/rollback.
GUI tests use a small XAPI substitute to check event ordering, captured releases,
modal dialogs, selection movement, resize and bounded display uploads. This
complements the freestanding ELF build; it does not replace an OS runtime test.

For a QEMU check, launch xPhoton from the launcher and exercise a fast brush
stroke across an edge, secondary-color drawing, shape cancellation, fill, an
overlapping selection move and undo, PNG/BMP save/reopen, zoom/pan, a window
resize during a drag, unsaved File > Exit and two simultaneous instances.
