# Rainbow

Runs unmodified Atari TOS/GEM programs through the built executable.

**Course:** COMP4003 MSc Project  
**Author:** Christopher Cortez  
**Supervisor:** Dr. Steven Bagley  

## What It Is

Rainbow runs an Atari ST program's real 68000 machine code with [Musashi](https://github.com/kstenerud/Musashi) and translates each operating system call it makes (GEMDOS, BIOS, XBIOS, AES, VDI) to the Win32 equivalent or resolved within the subsystem handlers. The guest program executes from within `Rainbow.exe`'s memory space.

Rainbow is currently single-threatened and rejects most MiNT calls.

## How It Works

### Trap interception

At startup each trap family's vector is pointed at a reserved address just past the exception vector table. Musashi's instruction hook checks the program counter every instruction; when it reaches one of those addresses the matching dispatcher runs. Nothing is decoded at the address itself.

| Vector | Family | Convention |
|---|---|---|
| `TRAP #1` | GEMDOS | function number and args on the stack |
| `TRAP #2` | AES / VDI | `D0` picks the family (`0xC8` AES, `0x73` VDI); `D1` is a parameter block |
| `TRAP #13` | BIOS | function number and args on the stack |
| `TRAP #14` | XBIOS | function number and args on the stack |

GEMDOS, BIOS and XBIOS use one shared lookup (`trap_table.c`). AES and VDI have separate dispatch tables because their parameter blocks differ (AES six pointers, VDI five).

CPU faults (bus error, address error, illegal instruction, zero divide, CHK, TRAPV, privilege violation, Line-A, Line-F) each get their own reserved address, since the exception frame carries no function number. They log the registers and recent instruction history, then halt.

### Screen

The ST has one screen. The menu bar and every window are rectangles in one framebuffer, ordered front to back by the AES. Rainbow uses one host window with one off-screen bitmap at 640x400 (shown at 2x), not one `HWND` per GEM window.

- A window's drawing is clipped to its rectangle minus the windows above it, so a covered window cannot draw over the one in front.
- `wind_get(WF_FIRSTXYWH/WF_NEXTXYWH)` returns the real list of visible sub-rectangles, so the standard GEM redraw loop repaints only exposed areas.
- Title bars, boxes and sliders are drawn onto the canvas. Dragging one tracks an outline and sends the app a message (`WM_MOVED`, `WM_SIZED`, and so on); the window moves only when the app calls `wind_set` back.
- The menu bar strip is never drawn into or clipped by any window.

The canvas keeps every pixel drawn, so `WM_PAINT` never asks the guest to redraw. Redraw messages go out only when window management exposes something.

## Layout

```
Guest binary (68000 code)
Musashi 68k core  +  memory (16 MiB flat array)
Trap dispatch
GEMDOS  BIOS  XBIOS  AES  VDI
Screen canvas (under AES and VDI)
Win32
```

Dependencies point one way only. The AES reads from the VDI, BIOS and GEMDOS; the BIOS shares GEMDOS's handle table. The AES and VDI draw only through the canvas, never Win32 directly; the AES gives the canvas a few callbacks at startup so it can resolve which window a draw belongs to.

| Path | Contents |
|---|---|
| `src/main.c` | startup, command line, execution loop |
| `src/tos_layer.c` | reserved addresses, instruction hook, fault handling |
| `src/trap_table.c` | shared lookup for GEMDOS/BIOS/XBIOS |
| `src/memory.c` | 16 MiB address space, big-endian access |
| `src/prg_loader.c` | executable parsing, relocation, basepage |
| `src/screen_canvas.c` | host window, bitmap, drawing, fonts, clipping |
| `src/gemdos/` | process control, console and file I/O, directories, heap |
| `src/bios/` | console and device I/O, keyboard state |
| `src/xbios/` | `Supexec`, `Getrez` |
| `src/aes/` | app lifecycle, windows, events, menus, resources, objects, dialogs |
| `src/vdi/` | drawing primitives and state |
| `src/logger.c` | trace output |
| `external/musashi/` | 68000 core (vendored) |

`include/` mirrors `src/`.

## Build

Needs CMake 3.10+ and a C compiler (MinGW-w64 or MSVC).

```
cmake -B build
cmake --build build
```

A codegen step builds `m68kmake` and runs it on Musashi's instruction table to produce `m68kops.c/h` before the main target. The executable is `build/bin/Rainbow.exe`.

```
Rainbow.exe path\to\program.prg [--debug]
```

`--debug` writes `logs/<program>_<timestamp>.log`: one line per trap and call, with call site, the Win32 API it mapped to, and the result. It is the primary way to see why a program failed.

## Bundled Binaries

Test programs (vbcc cross-compiled; sources for the last two in `test_programs/src/`):

| Binary | Exercises |
|---|---|
| `hello.tos` | loading, console output, termination |
| `file_io_test.tos` | file I/O, `Malloc`/`Mfree`/`Mshrink` |
| `occlusion_test.tos` | two overlapping windows: `wind_*`, z-order, move/size messages, `vs_clip`, the `WF_*XYWH` redraw loop |
| `painter.tos` | toolbar and freehand canvas: `v_opnvwk`, `vs_color`, `evnt_multi` buttons, `graf_mkstate`, stroke replay on redraw |

Real software (`test_programs/real_programs/`, unmodified):

| Program | Notes |
|---|---|
| `1st_word/1ST_WORD.PRG` | 1st Word (GST) |
| `wordplus/WORDPLUS.PRG` | 1st Word Plus 3.20 (GST) |

## Opcode Table

Status of every call Rainbow recognizes; the same tags are in the source at each handler.

- **Full**: matches TOS within Rainbow's scope.
- **Partial**: works, with the stated gap.
- **No-op**: accepted and answered, changes nothing.
- **Unimpl**: named for the trace only; does nothing.

62 full, 24 partial, 13 no-op, 10 unimpl (109 total).

| Opcode | Call | Status |
|---|---|---|
| `0x00` | `Pterm0` | Full |
| `0x02` | `Cconout` | Full |
| `0x09` | `Cconws` | Full |
| `0x19` | `Dgetdrv` | Full |
| `0x1A` | `Fsetdta` | Full |
| `0x20` | `Super` | Partial. Only ever enters or stays in supervisor mode |
| `0x2F` | `Fgetdta` | Full |
| `0x39` | `Dcreate` | Full |
| `0x3C` | `Fcreate` | Partial. Attribute bits ignored |
| `0x3D` | `Fopen` | Partial. Sharing and inheritance bits not enforced |
| `0x3E` | `Fclose` | Full |
| `0x3F` | `Fread` | Full |
| `0x40` | `Fwrite` | Full |
| `0x42` | `Fseek` | Full |
| `0x47` | `Dgetpath` | Partial. Always the root; `Dsetpath` is unimplemented |
| `0x48` | `Malloc` | Full |
| `0x49` | `Mfree` | Full |
| `0x4A` | `Mshrink` | Full |
| `0x4C` | `Pterm` | Full |
| `0x4E` | `Fsfirst` | Partial. First match only; `Fsnext` is unimplemented |
| `0x104` | `Fcntl` | No-op. MiNT-era call the C runtime makes at startup |

### BIOS (`TRAP #13`)

| Opcode | Call | Status |
|---|---|---|
| `0x01` | `Bconstat` | Partial. Always reports a character waiting |
| `0x02` | `Bconin` | Full |
| `0x03` | `Bconout` | Full |
| `0x08` | `Bcostat` | Full |
| `0x0A` | `Drvmap` | Full. One drive (`C:`) |
| `0x0B` | `Kbshift` | Full |

Only the console device (2) is modeled; PRT, AUX, MIDI and IKBD are ignored.

### XBIOS (`TRAP #14`)

| Opcode | Call | Status |
|---|---|---|
| `0x04` | `Getrez` | Partial. Reports ST high, which the 16-color canvas is not |
| `0x26` | `Supexec` | Full |

### AES (`TRAP #2`, `D0` = `0xC8`)

| Opcode | Call | Status |
|---|---|---|
| `0x0A` | `appl_init` | Full |
| `0x13` | `appl_exit` | No-op. Nothing is registered to unregister |
| `0x14` | `evnt_keybd` | Unimpl |
| `0x15` | `evnt_button` | Partial. Left button only, no multi-click counting |
| `0x16` | `evnt_mouse` | Full |
| `0x17` | `evnt_mesag` | Full |
| `0x18` | `evnt_timer` | Full |
| `0x19` | `evnt_multi` | Partial. `MU_M1`/`MU_M2` unchecked; one event per call |
| `0x1E` | `menu_bar` | Full |
| `0x1F` | `menu_icheck` | Full |
| `0x20` | `menu_ienable` | Full |
| `0x21` | `menu_tnormal` | Full |
| `0x22` | `menu_text` | Full |
| `0x2A` | `objc_draw` | Full |
| `0x2B` | `objc_find` | Full |
| `0x2C` | `objc_offset` | Full |
| `0x32` | `form_do` | Full |
| `0x33` | `form_dial` | Partial. No zoom animation on grow/shrink |
| `0x34` | `form_alert` | Full. Via `MessageBoxA` |
| `0x36` | `form_center` | Full |
| `0x46` | `graf_rubberbox` | Full |
| `0x47` | `graf_dragbox` | Unimpl. Reached by 1st Word Plus |
| `0x49` | `graf_growbox` | Full |
| `0x4D` | `graf_handle` | Full |
| `0x4E` | `graf_mouse` | Partial. Shapes map to host cursors; hiding is ignored |
| `0x4F` | `graf_mkstate` | Full |
| `0x50` | `scrp_read` | Full |
| `0x51` | `scrp_write` | Full |
| `0x5A` | `fsel_input` | Partial. Delegates to the host file dialog |
| `0x64` | `wind_create` | Full |
| `0x65` | `wind_open` | Full |
| `0x66` | `wind_close` | Full |
| `0x67` | `wind_delete` | Full |
| `0x68` | `wind_get` | Partial. Geometry, sliders and rectangle list; other fields zeroed |
| `0x69` | `wind_set` | Partial. Title, geometry, z-order, sliders, `WF_NEWDESK` |
| `0x6A` | `wind_find` | Full |
| `0x6B` | `wind_update` | No-op. Nothing concurrent to serialize against |
| `0x6C` | `wind_calc` | Full |
| `0x6E` | `rsrc_load` | Full |
| `0x6F` | `rsrc_free` | Full |
| `0x70` | `rsrc_gaddr` | Partial. Six resource types addressable |
| `0x78` | `shel_read` | Partial. Always an empty command line |
| `0x7C` | `shel_find` | Full |

Calls not in this table are unimplemented and logged as `Unknown` with their opcode.

### VDI (`TRAP #2`, `D0` = `0x73`)

| Opcode | Call | Status |
|---|---|---|
| `0x02` | `v_clswk` | Unimpl |
| `0x03` | `v_clrwk` | No-op. Every repaint already clears |
| `0x06` | `v_pline` | Full |
| `0x08` | `v_gtext` | Partial. Long strings truncated |
| `0x09` | `v_fillarea` | Unimpl. Reached by 1st Word |
| `0x0B` | `v_gdp` | Partial. `v_bar` only of the ten primitives; `v_justified` is the notable gap |
| `0x0C` | `vst_height` | Partial. Metrics answered, font size fixed |
| `0x0D` | `vst_rotation` | Unimpl |
| `0x0E` | `vs_color` | Full |
| `0x0F` | `vsl_type` | No-op |
| `0x10` | `vsl_width` | No-op |
| `0x11` | `vsl_color` | Full |
| `0x13` | `vsm_height` | No-op |
| `0x15` | `vst_font` | Unimpl |
| `0x16` | `vst_color` | Full |
| `0x17` | `vsf_interior` | Full. Hollow vs solid; patterns collapse to solid |
| `0x18` | `vsf_style` | Unimpl. Reached by 1st Word |
| `0x19` | `vsf_color` | Full |
| `0x1A` | `vq_color` | Full |
| `0x20` | `vswr_mode` | Full. Replace and XOR |
| `0x27` | `vst_alignment` | Full |
| `0x64` | `v_opnvwk` | Partial. Only the commonly checked fields answered |
| `0x65` | `v_clsvwk` | No-op |
| `0x66` | `vq_extnd` | Partial. Approximated capability array |
| `0x68` | `vsf_perimeter` | No-op |
| `0x6A` | `vst_effects` | Partial. Outlined and shadowed not drawn |
| `0x6B` | `vst_point` | Unimpl |
| `0x6C` | `vsl_ends` | No-op |
| `0x6D` | `vro_cpyfm` | Partial. Screen-to-screen only; off-screen forms rejected |
| `0x71` | `vsl_udsty` | No-op |
| `0x72` | `vr_recfl` | Full |
| `0x74` | `vqt_extent` | Unimpl |
| `0x79` | `vrt_cpyfm` | Unimpl |
| `0x7A` | `v_show_c` | No-op. Cursor hiding is disabled |
| `0x7B` | `v_hide_c` | No-op. See `v_show_c` |
| `0x81` | `vs_clip` | Full |
| `0x83` | `vqt_fontinfo` | Partial. The canvas's fixed 8x16 cell |

## Limitations

Beyond the per-call gaps above:

- **Line-A halts the guest.** Atari's low-level graphics interface uses the 68000's `$A` opcode space. Rainbow reserves the vector but treats it as a fault, so software that uses it (mostly games) stops rather than degrading. Atari deprecated Line-A in favor of the VDI, which Rainbow implements, but real software still used it.
- **Off-screen forms in `vro_cpyfm` are rejected**, so bundled `.IMG` clip art does not render.
- **The canvas is not a real ST mode.** It reports 16 colors while `Getrez` reports ST high, which was monochrome. A resource asking for color index 2 gets a color, not black.
