# D64BSP

D64BSP is a node builder for Doom 64 maps. It is glBSP 2.24 by Andrew Apted, adapted to the Doom 64 map format by
Samuel Villarreal (Kaiser) in 2011. It builds the lumps VERTEXES, SEGS, SSECTORS, NODES, LEAFS, BLOCKMAP and REJECT
of a map. [Doom Builder 64 II](https://github.com/Styd051/Doom-Builder-64-II) runs it when a map is saved.

MAP34 to MAP40 of the 2020 remaster were built with this program: it gives back the 13 lumps of these 7 maps, byte
for byte.

## History

- The first commit of this repository (`dc2aa55`) is the source code of D64BSP as Kaiser left it.
- In 2026 it was checked against the code of the Doom 64 engine (the reverse engineering
  [DOOM64-RE](https://github.com/Erick194/DOOM64-RE) of Erick194), Doom64 EX and the 2020 remaster:
  - `aa9de18`: all the lines over the same two points get their segs, and only one of them goes into LEAFS, as
    Midway did; two lines in the same direction at a vertex no longer leave a polygon open; the partitions of
    lines with fractions stay close to the true line.
- A map with no overlapping lines and no fractions in the vertices of its lines gives the same lumps as the
  version of Kaiser.

## Building

From an x86 prompt of Visual Studio, in the folder of the sources:

```
cl /nologo /O2 /W3 /MT /arch:IA32 /D WIN32 /D NDEBUG /D _CONSOLE /D _CRT_SECURE_NO_WARNINGS *.c /FeD64BSP.exe
```

With CMake:

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Build it for 32 bits. `/arch:IA32` keeps the floating point of the x87, which `CMakeLists.txt` sets for a 32-bit
build. With SSE2, or in a 64-bit build (the default of CMake without `-A Win32`), the angle of a few segs changes
by one step in 65536: the maps still work, but MAP35 and MAP38 are then no longer the bytes of the game.

`msvc6` holds the Visual C++ 6 project of Kaiser, and `Makefile` his gcc build. They were not tried in 2026.

## Command line

```
D64BSP input.wad -o output.wad [options]
D64BSP @file
```

`input.wad` and `output.wad` can be the same file, which is what Doom Builder 64 II does. Always give `-o`: without
it the program only writes an empty `.gwa` file. `@file` reads the arguments from a text file.

| Option | What it does |
|---|---|
| `-q` | Fewer messages |
| `-w` | Shows the minor warnings |
| `-xp` | No progress bar |
| `-c n` | Cost of splitting a seg when a partition is chosen, 11 by default |
| `-b n` | Number of blocks above which the BLOCKMAP is cut, 16000 by default, from 1000 to 64000 |
| `-xr` | Keeps the REJECT of the input |
| `-m` | Merges the vertices at the same place |
| `-u` | Removes the sectors that no side uses, which renumbers the sectors after them |
| `-xu` | Keeps the sides that no line uses, and lines of no length |

`-f`, `-n`, `-xn`, `-v2`, `-v3` and `-v5` do nothing here: D64BSP always builds the nodes. `-y` changed nothing on
the maps it was tried on.

**Do not use these options on a Doom 64 map:**

| Option | Why |
|---|---|
| `-p` | Merges sides without comparing their textures, and even on lines with an action: walls lose their textures. It changed 702 sides of lines on MAP01 |
| `-s` | Skips the lines with the same sector on both sides: these lines get no angle in the engines |
| `-v1` | Uses an old rounding of glBSP for the segs, which changes VERTEXES, SEGS and LEAFS |
| `-hexen` | Reads and writes the lines in the format of Hexen, which changes LINEDEFS |

## Maps

A map is found by its name lump, followed by the lumps of the map. They are written in the order of the game, the
order in which all the engines read them. The WAD stays a PWAD or an IWAD. Other lumps stay where they are, such as
a `SCRIPTS` lump after `MACROS`.

| Lump | Size | D64BSP |
|---|---|---|
| `THINGS` | 14 bytes each | Copied |
| `LINEDEFS` | 16 | Rewritten, without the lines of no length |
| `SIDEDEFS` | 12 | Rewritten, without the sides that no line uses |
| `VERTEXES` | 8, 16.16 fixed point | The vertices of the lines, then a copy of each new vertex that nothing uses, then the new vertices |
| `SEGS` | 12 | Built |
| `SSECTORS` | 4 | Built |
| `NODES` | 28, whole numbers | Built |
| `SECTORS` | 24 | Copied |
| `REJECT` | one bit for each pair of sectors | Built: only the groups of sectors that do not touch |
| `BLOCKMAP` | 16-bit numbers | Built |
| `LEAFS` | a count, then (vertex, seg) for each corner | Built: the polygon of each subsector, clockwise, seg -1 for an edge without a wall |
| `LIGHTS` | 6 | Copied |
| `MACROS` | see [D64BLAM](https://github.com/Styd051/D64BLAM) | Copied |

## What the engines do with these lumps

The three engines read the same sizes and the same fields. They differ here:

| | Nintendo 64 (DOOM64-RE) | Doom64 EX | Remaster |
|---|---|---|---|
| Numbers of vertices, segs and subsectors | Signed, 32767 at most | Unsigned | Unsigned |
| Angle of a line | From a seg that starts at the first vertex of the line, 0 if there is none | From the last seg read for the line, 0 if there is none | Same as the Nintendo 64 |
| Side of a point | Whole units for the point and the partition | The point keeps its fraction | Same as the Nintendo 64 |
| Corners drawn for a floor | 32 at most | No limit | No limit |
| Leaf of fewer than 3 corners | Its walls are drawn | Skipped, with its walls | Skipped, with its walls |
| Bad BLOCKMAP | Not checked | Error when the map loads | Error when the map loads |

They agree on the rest:

- Walls are drawn from LEAFS. The Nintendo 64 also walks SEGS to know what is hidden.
- Monsters look through SEGS, not LEAFS, and through REJECT first.
- The angle of a line is what a thing slides along when it hits that line.
- The sector of a subsector comes from its first seg.
- A list of the BLOCKMAP is read from its first number, and its offsets are signed 16-bit numbers.
- A switch is placed at the middle of its line, even when segs cut that line.

## Differences from the maps of Midway

MAP01 to MAP33 were built with the tool of Midway. D64BSP does not give the same lumps, and the maps still work:

| | Midway | D64BSP |
|---|---|---|
| Offset of a seg that does not start on a whole number | Rounded down | Rounded up, as in MAP34 to MAP40 |
| New vertices | Written once | Written twice, the first copy is not used |
| BLOCKMAP origin | 8 units before the map | At the map, rounded down to a multiple of 8 |
| BLOCKMAP lists | The line 0 when it is there | Always start with the line 0, which tests it for nothing |
| REJECT | A partial table, its method is not known | The groups of sectors that do not touch |
| Direction of a partition | Scaled to a length of 256 | The line itself |
| Vertices on the wrong side of a partition, worst case on MAP01 to MAP33 | 3.43 units | 0.50 units |

## Limits

- 32 corners for a floor on the Nintendo 64. D64BSP does not check it. The largest leaf of the maps of the game has 30.
- 32767 vertices, segs and subsectors on the Nintendo 64. D64BSP does not check the vertices.
- The offsets of the BLOCKMAP are signed in the three engines, but D64BSP accepts them up to 65535 and writes an
  empty lump above that. The largest seen is 15250, in a map of the Mzansi project.
- Above 16000 blocks the BLOCKMAP is cut, and lines outside of it no longer stop things. Use `-b` for a larger map.
- Sectors and lines with the tags 900 to 999 keep the rules of glBSP for special effects of Doom: such a line is
  avoided as a partition. The maps of the game do not need this.

## Files

| File | Holds |
|---|---|
| `main.c` | The command line |
| `glbsp.c` | The options, and the work on one map |
| `wad.c` | Reading and writing the WAD, the list and the order of the lumps |
| `level.c` | The lumps of a map, `PutLeafs` |
| `analyze.c` | Removing lines, sides and vertices, overlapping lines, the walls around each vertex |
| `seg.c` | Choosing a partition, cutting segs |
| `node.c` | The tree, the order of the segs of a subsector, `FitPartition`, `MarkHiddenSegs` |
| `blockmap.c`, `reject.c` | BLOCKMAP and REJECT |
| `display.c`, `system.c`, `util.c` | Messages, memory, helpers |

## Credits

- Samuel Villarreal (Kaiser): D64BSP.
- Andrew Apted: glBSP.
- Colin Reed, Lee Killough and the authors of BSP, listed in `AUTHORS`; Raphael Quinet for DEU.
- The Nintendo 64 engine as read in DOOM64-RE by Erick194.

This program is free software, under the terms of the GNU General Public License, version 2 or later.
