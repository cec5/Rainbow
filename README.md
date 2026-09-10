# Rainbow

Native execution of legacy Atari TOS/GEM applications on modern Windows.

**Course:** COMP4003 MSc Project  
**Author:** Christopher Cortez  
**Supervisor:** Dr. Steven Bagley  

## What This Is

Rainbow is a compatibility layer that runs unmodified Atari ST software as an ordinary Windows process.

The guest program's real Motorola 68000 machine code is executed instruction by instruction by [Musashi](https://github.com/kstenerud/Musashi). Every call it makes to its operating system, whether GEMDOS, BIOS, XBIOS, AES or VDI, is intercepted at the point the trap occurs and serviced by native Win32 code.

## How It Works

### Trap interception

Each trap family is given a reserved address just past the 68000's exception vector table, written into the vector table at startup. Musashi's instruction-hook callback fires once per instruction and recognises the moment the program counter reaches one of those addresses, before any real opcode is fetched from it. Nothing is ever decoded there.

| Vector | Family | Calling convention |
|---|---|---|
| `TRAP #1` | GEMDOS | function number and arguments on the stack |
| `TRAP #2` | AES / VDI | `D0` selects the family (`0xC8` AES, `0x73` VDI); `D1` points at a parameter block |
| `TRAP #13` | BIOS | function number and arguments on the stack |
| `TRAP #14` | XBIOS | function number and arguments on the stack |

GEMDOS, BIOS and XBIOS are reached identically, so all three share one lookup routine (`trap_table.c`). AES and VDI keep their own dispatch tables, since they are parameter-block calls with different layouts: the AES block holds six pointers, the VDI's five.

CPU faults (bus error, address error, illegal instruction, zero divide, CHK, TRAPV, privilege violation, Line-A, Line-F) each get their own reserved address, because a fault's exception frame carries no function number to identify it by. They log the registers and recent instruction history, then halt.

### The screen

A real ST has exactly one screen. The menu bar and every GEM window are regions of the same framebuffer, stacked front to back in an order the AES maintains. The layer models this directly rather than giving each GEM window its own `HWND`.

- **One window, one canvas.** A single host window holds one off-screen bitmap at the guest's resolution (640x400, presented at `DISPLAY_SCALE` 2x). Every AES window is a rectangle within it, so a draw call's screen coordinates are already canvas coordinates.
- **Occlusion by construction.** A draw belonging to window *N* is clipped to *N*'s rectangle minus the rectangles of everything above it. A covered window physically cannot paint over the one in front, whatever order the application redraws in.
- **The rectangle list is real.** `wind_get(WF_FIRSTXYWH/WF_NEXTXYWH)` walks the actual decomposition of a partly covered window, so applications using the canonical GEM redraw loop repaint only their exposed pieces.
- **Chrome reports, it does not act.** Dragging a title bar tracks an XOR outline and then sends `WM_MOVED`; nothing actually moves until the application calls `wind_set` back. This is what the AES does, and moving a window behind an application's back would leave it repainting at stale coordinates.
- **The menu bar owns a reserved strip** that no window may paint into and none of them clip.

Because the canvas holds every pixel the guest has drawn, a host `WM_PAINT` never has to ask the guest to redraw. Redraw messages are issued only when window management actually exposes something.

## Code Structure

The layer is a stack, each level depending only on those below it:

```
                    Guest binary (68000 machine code)
       Musashi 68k core  <-->  Memory subsystem (16 MiB flat array)
                          Trap dispatch layer
        GEMDOS   BIOS   XBIOS   AES   VDI     (subsystem modules)
                    Screen canvas (beneath AES and VDI)
                            Win32 API
```

Dependencies run in one direction only and form no cycles. The AES reads drawing attributes from the VDI, keyboard state from the BIOS and heap memory from GEMDOS; the BIOS shares GEMDOS's open-handle table. Nothing points back upward.

The two modules that draw never touch the Win32 drawing API directly. Both go through the screen canvas, which owns the single screen the guest sees. The canvas cannot decide which window a draw belongs to, because under GEM only the AES knows that, so the AES installs a small set of function pointers at startup that answer the questions the canvas needs.

| Path | Contents |
|---|---|
| `src/main.c` | Startup, command line, the top-level execution loop |
| `src/tos_layer.c` | Reserved dispatch addresses, the instruction hook, fault handling |
| `src/trap_table.c` | Shared lookup for the three register-based families |
| `src/memory.c` | The flat 16 MiB address space, big-endian accessors |
| `src/prg_loader.c` | GEMDOS executable parsing, relocation, basepage construction |
| `src/screen_canvas.c` | Host window, off-screen bitmap, drawing primitives, fonts, clipping |
| `src/gemdos/` | Process control, console and file I/O, directories, heap allocation |
| `src/bios/` | Low-level console and device I/O, keyboard shift state |
| `src/xbios/` | `Supexec` and `Getrez` |
| `src/aes/` | Application lifecycle, windowing, events, menus, resources, objects, dialogs |
| `src/vdi/` | Graphics primitives and drawing state |
| `src/logger.c` | Categorised trace output |
| `include/` | Mirrors `src/` one-for-one |
| `external/musashi/` | The 68000 core (vendored) |

## Building

Requires CMake 3.10+ and a C compiler (MinGW-w64 GCC or MSVC).

```
cmake -B build
cmake --build build
```

The build defines two targets. `m68kmake` is a small utility compiled from Musashi's own sources; a custom build step runs it against Musashi's instruction table to generate the opcode dispatch (`m68kops.c/h`) into the build tree before the main target compiles. `Rainbow` is the layer itself, linked against `user32`, `gdi32` and `comdlg32`.

The executable lands in `build/bin/`.

### Running

```
Rainbow.exe path\to\program.prg [--debug]
```

`--debug` writes a full trace to `logs/<program>_<timestamp>.log`: one sequence-numbered, timestamped line per event, covering every trap with its call site, every call by name with what it mapped to and what it returned, and every error. This log is the primary debugging tool, since a guest program that misbehaves gives no other indication of why.

## Bundled Atari Binaries

Purpose-built test programs, cross-compiled with vbcc (sources in `test_programs/src/` where retained):

| Binary | Exercises |
|---|---|
| `test_programs/hello.tos` | Program loading, console output, clean termination |
| `test_programs/file_io_test.tos` | `Fcreate`/`Fopen`/`Fread`/`Fwrite`/`Fseek`/`Fclose`, `Malloc`/`Mfree`/`Mshrink` |
| `test_programs/occlusion_test.tos` | Two overlapping windows with striped fills: `wind_*`, z-order, `WM_TOPPED`/`WM_MOVED`/`WM_SIZED`, `vs_clip`, and the `WF_FIRSTXYWH`/`WF_NEXTXYWH` redraw loop |
| `test_programs/painter.tos` | A paint program with a colour-swatch toolbar: `v_opnvwk`, `vs_color`, `evnt_multi`'s `MU_BUTTON`, `graf_mkstate`, freehand drawing with stroke replay on redraw |

Authentic commercial software, unmodified, under `test_programs/real_programs/`:

| Program | Notes |
|---|---|
| `1st_word/1ST_WORD.PRG` | 1st Word, the original GST word processor |
| `wordplus/WORDPLUS.PRG` | 1st Word Plus 3.20 (GST). The layer's primary integration test: resource loading, the menu bar, `form_do` dialogs, `fsel_input`, and sustained interactive editing |

## Master Opcode Table

Every call the layer recognises, with its implementation status. The same tags appear in the source at each handler's definition, alongside the reason, so `grep -rn "\[PARTIAL\]" src` reproduces the gaps listed here.

- **Full** — behaves as TOS specifies, within the layer's scope.
- **Partial** — works, with a stated gap.
- **No-op** — accepted and answered, but changes nothing.
- **Unimplemented** — named so the trace can report it; nothing happens.

Totals: **62 full, 24 partial, 13 no-op, 7 unimplemented** across 106 calls.

### GEMDOS (`TRAP #1`)

| Opcode | Call | Status |
|---|---|---|
| `0x00` | `Pterm0` | Full |
| `0x02` | `Cconout` | Full |
| `0x09` | `Cconws` | Full |
| `0x19` | `Dgetdrv` | Full |
| `0x1A` | `Fsetdta` | Full |
| `0x20` | `Super` | Partial — only ever enters or stays in supervisor mode |
| `0x2F` | `Fgetdta` | Full |
| `0x39` | `Dcreate` | Full |
| `0x3C` | `Fcreate` | Partial — attribute bits ignored |
| `0x3D` | `Fopen` | Partial — sharing and inheritance fields not enforced |
| `0x3E` | `Fclose` | Full |
| `0x3F` | `Fread` | Full |
| `0x40` | `Fwrite` | Full |
| `0x42` | `Fseek` | Full |
| `0x47` | `Dgetpath` | Partial — always the root, as `Dsetpath` is unimplemented |
| `0x48` | `Malloc` | Full |
| `0x49` | `Mfree` | Full |
| `0x4A` | `Mshrink` | Full |
| `0x4C` | `Pterm` | Full |
| `0x4E` | `Fsfirst` | Partial — only the first match, as `Fsnext` is unimplemented |
| `0x104` | `Fcntl` | No-op — MiNT-era call the C runtime makes at startup |

### BIOS (`TRAP #13`)

| Opcode | Call | Status |
|---|---|---|
| `0x01` | `Bconstat` | Partial — always reports a character waiting |
| `0x02` | `Bconin` | Full |
| `0x03` | `Bconout` | Full |
| `0x08` | `Bcostat` | Full |
| `0x0A` | `Drvmap` | Full — one drive (`C:`) |
| `0x0B` | `Kbshift` | Full |

Only the console device (2) is modelled; PRT, AUX, MIDI and IKBD are ignored.

### XBIOS (`TRAP #14`)

| Opcode | Call | Status |
|---|---|---|
| `0x04` | `Getrez` | Partial — reports ST high, which the 16-colour canvas is not |
| `0x26` | `Supexec` | Full |

### AES (`TRAP #2`, `D0` = `0xC8`)

| Opcode | Call | Status |
|---|---|---|
| `0x0A` | `appl_init` | Full |
| `0x13` | `appl_exit` | No-op — nothing is registered to unregister |
| `0x15` | `evnt_button` | Partial — left button only, no multi-click counting |
| `0x16` | `evnt_mouse` | Full |
| `0x17` | `evnt_mesag` | Full |
| `0x18` | `evnt_timer` | Full |
| `0x19` | `evnt_multi` | Partial — `MU_M1`/`MU_M2` unchecked; one event per call |
| `0x1E` | `menu_bar` | Full |
| `0x1F` | `menu_icheck` | Full |
| `0x20` | `menu_ienable` | Full |
| `0x21` | `menu_tnormal` | Full |
| `0x22` | `menu_text` | Full |
| `0x2A` | `objc_draw` | Full |
| `0x2B` | `objc_find` | Full |
| `0x2C` | `objc_offset` | Full |
| `0x32` | `form_do` | Full |
| `0x33` | `form_dial` | Partial — no zoom animation on grow/shrink |
| `0x34` | `form_alert` | Full — via `MessageBoxA` |
| `0x36` | `form_center` | Full |
| `0x46` | `graf_rubberbox` | Full |
| `0x49` | `graf_growbox` | Full |
| `0x4D` | `graf_handle` | Full |
| `0x4E` | `graf_mouse` | Partial — shapes map to host cursors, hiding is ignored |
| `0x4F` | `graf_mkstate` | Full |
| `0x50` | `scrp_read` | Full |
| `0x51` | `scrp_write` | Full |
| `0x5A` | `fsel_input` | Partial — delegates to the host file dialog |
| `0x64` | `wind_create` | Full |
| `0x65` | `wind_open` | Full |
| `0x66` | `wind_close` | Full |
| `0x67` | `wind_delete` | Full |
| `0x68` | `wind_get` | Partial — geometry, sliders and rectangle list; other fields zeroed |
| `0x69` | `wind_set` | Partial — title, geometry, z-order, sliders, `WF_NEWDESK` |
| `0x6A` | `wind_find` | Full |
| `0x6B` | `wind_update` | No-op — nothing concurrent to serialise against |
| `0x6C` | `wind_calc` | Full |
| `0x6E` | `rsrc_load` | Full |
| `0x6F` | `rsrc_free` | Full |
| `0x70` | `rsrc_gaddr` | Partial — six resource types addressable |
| `0x78` | `shel_read` | Partial — always an empty command line |
| `0x7C` | `shel_find` | Full |

Calls absent from this table, `evnt_keybd` among them, are unimplemented and reported by opcode number only.

### VDI (`TRAP #2`, `D0` = `0x73`)

| Opcode | Call | Status |
|---|---|---|
| `0x02` | `v_clswk` | Unimplemented |
| `0x03` | `v_clrwk` | No-op — every repaint already clears |
| `0x06` | `v_pline` | Full |
| `0x08` | `v_gtext` | Partial — long strings truncated |
| `0x09` | `v_fillarea` | Unimplemented |
| `0x0B` | `v_gdp` | Partial — only `v_bar` of the ten primitives; `v_justified` is the notable gap |
| `0x0C` | `vst_height` | Partial — metrics answered, font size fixed |
| `0x0D` | `vst_rotation` | Unimplemented |
| `0x0E` | `vs_color` | Full |
| `0x0F` | `vsl_type` | No-op |
| `0x10` | `vsl_width` | No-op |
| `0x11` | `vsl_color` | Full |
| `0x13` | `vsm_height` | No-op |
| `0x15` | `vst_font` | Unimplemented |
| `0x16` | `vst_color` | Full |
| `0x17` | `vsf_interior` | Full — hollow versus solid; patterns collapse to solid |
| `0x19` | `vsf_color` | Full |
| `0x1A` | `vq_color` | Full |
| `0x20` | `vswr_mode` | Full — replace and XOR |
| `0x27` | `vst_alignment` | Full |
| `0x64` | `v_opnvwk` | Partial — only the commonly checked fields answered |
| `0x65` | `v_clsvwk` | No-op |
| `0x66` | `vq_extnd` | Partial — approximated capability array |
| `0x68` | `vsf_perimeter` | No-op |
| `0x6A` | `vst_effects` | Partial — outlined and shadowed not drawn |
| `0x6B` | `vst_point` | Unimplemented |
| `0x6C` | `vsl_ends` | No-op |
| `0x6D` | `vro_cpyfm` | Partial — screen-to-screen only, off-screen forms rejected |
| `0x71` | `vsl_udsty` | No-op |
| `0x72` | `vr_recfl` | Full |
| `0x74` | `vqt_extent` | Unimplemented |
| `0x79` | `vrt_cpyfm` | Unimplemented |
| `0x7A` | `v_show_c` | No-op — cursor hiding deliberately disabled |
| `0x7B` | `v_hide_c` | No-op — see `v_show_c` |
| `0x81` | `vs_clip` | Full |
| `0x83` | `vqt_fontinfo` | Partial — the canvas's fixed 8x16 cell |

## Known Limitations

Beyond the per-call gaps above:

- **Line-A instructions halt the guest.** Atari's low-level graphics interface occupies the 68000's `$A` opcode space. The layer reserves the vector but routes it into the same handler as a genuine fault, so applications making those calls stops completely. Atari deprecated Line-A in favour of the VDI, which the layer does implement, but real software still used it.
- **Off-screen memory forms** in `vro_cpyfm` are rejected, so bundled `.IMG` clip art does not render.
- **The canvas is not a real ST mode.** It reports 16 colours while `Getrez` reports ST high, which was monochrome. A resource asking for colour index 2 gets a colour rather than black.
