# Lithium-only extensions

Lithium Lab, Packin and OutList are implemented in the C++ runtime (`runtime/src/primitives_lithium.cpp`),
described to the editor by `web/extensions.js`, and saved in a form other editors can open (`web/fallback.js`).
They run only in Lithium. In the built-in JavaScript runtime the blocks are harmless stubs.

## Lithium Lab
| Block | Meaning |
|---|---|
| `is TurboWarp?` | Always **false** in Lithium: true only if the project runs in TurboWarp's own runtime. |
| `is Compiled?` | **false**: true only when the project's scripts run as compiled code (TurboWarp's compiler). Lithium interprets scripts; that the interpreter is C++/WebAssembly does not count. |
| `is Lithium?` | **true**. |
| `is extension (name) available?` | Capability test: true when the runtime has blocks for that non-core extension (`pen`, `Packin`, `Lithium Lab`, `outlist`; case, spaces, `-` and `_` ignored). Core categories and unknown names give false. |

Boolean parameters named `is TurboWarp?`, `is compiled?` and `is Lithium?` answer the same way when no
custom-block parameter of that name exists (the TurboWarp convention), so they work anywhere in a project.
Unknown environments are never reported as TurboWarp or Lithium.

## Packin
Data is text. Packing uses the string's UTF-8 bytes, so any text, including binary data held as characters,
comes back exactly; the packed result is a *byte string* (one character U+0000..U+00FF per byte).

| Block | Meaning |
|---|---|
| `compress (data)` | Packed data, level 6. |
| `compress (data) at level (1-9)` | 1 fastest, 9 smallest (compression only; unpacking speed does not depend on level). |
| `decompress (data)` | Original text; empty if the input is not valid, intact Packin data. |
| `compressed size of (data)` | Bytes in the packed form of `data` (level 6). |
| `original size of (data)` | Original length in bytes, read from the validated header of packed data; 0 if invalid. |
| `(data) is valid Packin data?` | Header and length fields check out (cheap, does not decompress). |

Format (`runtime/include/lithium/packin.h`): `PKN`, version, mode, length, CRC-32, payload. `compress` tries every
mode and keeps the smallest: stored, LZ77 + canonical Huffman, and two numeric modes (integer lists with
delta + varint, decimal lists with scaled mantissas) that apply automatically when the data is a list of
numbers. Corrupt data is rejected by the CRC.

## OutList
Lists saved as files in a Lithium-managed area, scoped by project (`setStorageScope`; the GUI uses the
`project` scope until it passes the title).

| Block | Meaning |
|---|---|
| `save list [list] to file (name)` | Waits until the write completes. `last OutList error` is empty on success. |
| `load list from file (name) into [list]` | Replaces the list. Missing or damaged files report an error and leave the list untouched. |
| `saved list (name) exists?` | Answered from an index of saved names (names are read when the project starts and updated by this session's saves/deletes); it never reads list contents. |
| `delete saved list (name)` | Removes the file; a missing file is an error message, not a crash. |
| `unload list [list]` | Empties the list and frees its memory **only if** its saved file matches what is in memory. Never-saved lists and lists changed since the last save/load are refused (see the error). A list that was unloaded cannot be saved over its file until it is loaded again. |
| `last OutList error` | Text of the last OutList failure, `""` if it succeeded. |

Names: 1-100 bytes, no `/ \ : * ? " < > | %`, no control characters, no leading/trailing space or dot.
Files are `<area>/<name encoded to [a-z0-9_-]>.outlist`; encoding makes traversal and case-insensitive
collisions impossible. Writes are atomic (temp file + rename natively, one IndexedDB transaction in browsers).

**Honest limits.** This version writes and reads whole lists, so a list still needs to fit in memory *while*
it is loaded; `unload` is how a program keeps only what it is using in RAM. The file format is chunked
(4096 items per chunk, header with item count and chunk table, per-chunk Packin and CRC), so later versions can
add partial reads, ranges and append without a format change; none of that is implemented yet.
All operations are asynchronous (the script waits); `exists?` is a reporter and so synchronous by design.
The desktop app currently stores lists in its IndexedDB profile; native files are available through the
runtime (`lithium_set_storage_directory`) and are the next step for the desktop shell.

## Opening a project in another editor
On save, every Lithium-only block becomes a call to an empty custom block named exactly
`Lithium-only Extension block` (reporter and boolean shapes use TurboWarp's custom-reporter form), keeping
scripts and connections. The original opcode, inputs and fields stay on the block, the opcode is also in a
`lithiumop` mutation attribute, and `meta.lithium` holds a full restore map; the Lithium extensions are removed
from the project's extension list so other editors still open it. Lithium puts everything back on load. The
placeholder does nothing elsewhere. Not covered: sprites exported on their own (`.sprite3`) keep the real
opcodes.
