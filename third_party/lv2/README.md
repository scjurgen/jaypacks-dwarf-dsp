# LV2 headers (vendored)

The parts of the LV2 specification headers this project compiles against, so the Mac
build, the host tests and the Dwarf build all use the same headers.

| Path | LV2 module | Version |
|---|---|---|
| `lv2/core/lv2.h`, `lv2/core/lv2_util.h`, `lv2/core/attributes.h` | core | 18.0 (LV2 1.18) |
| `lv2/urid/urid.h` | urid | 1.4 (LV2 1.18) |
| `lv2/atom/atom.h`, `lv2/atom/util.h` | atom | 1.0 (LV2 1.18.2) |
| `lv2/midi/midi.h` | midi | 1.0 (LV2 1.18.2) |

Unchanged copies of the upstream headers from https://lv2plug.in (release archive
lv2-1.18.2.tar.bz2; the core and urid copies were compared with it and are identical). The
deprecated include path `lv2/lv2plug.in/ns/lv2core/lv2.h` is not vendored: code here includes
`lv2/core/lv2.h`.

Add further modules (log, ...) from the same LV2 release when a plugin needs them, with a row
above.

Licence: ISC, see `COPYING` (the same text is in every header).
