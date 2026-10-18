# n64-gs-aleck64-patch

[日本語](README.ja.md)

Patches a **GameShark Pro v3.30** (N64) firmware dump so that **Aleck64
(CIC-NUS-5101)** games can be started from the GameShark menu.

Supported builds (the build string is at header offset 0x30):

| Build          | shell.bin CRC32 |
|----------------|-----------------|
| `09:54 Mar 27` | `65B194D9`      |
| `15:56 Apr 4`  | `6E14E38B`      |

The stock firmware copies only the first 1 MB of the game to RAM before
jumping to it. The 5101 IPL3 instead loads 0x3FE000 bytes to 0x80000400 (when
the header entry point is 0x80100400), so Aleck64 games that rely on this do
not start through an unmodified GameShark.

## What the patch changes

Only `shell.bin` (the menu program, loaded at 0x80200400) is modified:

| Mar 27       | Apr 4        | Original                    | Patched                                   |
|--------------|--------------|-----------------------------|-------------------------------------------|
| `0x802005DC` | `0x802005E4` | start of the launch routine | `j 0x80205C00`                            |
| `0x80205C00` | `0x80205C00` | PC-link helpers that nothing calls (82 words) | new launch routine (79 words, see `launch.s`) |
| `0x80218410` | `0x80218A90` | `lui $k0, 4` in the loader stub | `or $k0, $a3, $zero` (word count from `$a3`) |

The launch routine is the same source for both builds, assembled with each
build's addresses (listed at the top of `launch.s`).

The new launch routine:

* detects the 5101 IPL3 by the words at ROM 0x524/0x528,
* subtracts 0x100000 from the header entry point, as the real 5101 IPL3 does
  (a `DE` code still overrides the entry point unchanged),
* when the entry point is then 0x80000400, copies **0x3FE000 bytes** instead of
  1 MB and places the loader stub and the F0/F1 patch list just below the stack
  at the top of RAM, above the copied area,
* launches every other game exactly as the original routine does.

The same routine is used with and without the code generator (trainer) enabled.

`shell.bin` is recompressed with a bit-exact reimplementation of the
firmware's LZARI packer (`lzari.c`; recompressing the original `shell.bin`
reproduces the original stream byte for byte) and the file table is rebuilt.
The bootloader, the other files, the settings block, the code list and the
data at 0x2EF00 are preserved.

Because the firmware bytes change, **every key code changes**. The tool
regenerates the key code table (Mario / Diddy / Zelda / Yoshi, plus a new
"Aleck64 (CIC-5101)" entry) and writes the key selected with `--boot` into the
header.

## Building

```sh
make
# or
cc -O2 -std=c99 -o gs_aleck64_patch gs_aleck64_patch.c lzari.c
```

## Usage

```sh
./gs_aleck64_patch --rom6105 <6105 ROM> [--boot <cic>] <input firmware> <output firmware>
```

* `--rom6105` (required): any ROM with a CIC-NUS-6105 IPL3 (only its first
  0x1000 bytes are read). It is needed to compute the "Zelda" key code.
* `--boot`: key code written to the header: `6102` (default), `6103`,
  `6105`, `6106` or `5101`. The GameShark only boots with a cartridge of the
  matching CIC on top. You can also switch keys later from the Key Codes menu.
* The input must be an unmodified v3.30 dump of a supported build (256 KB,
  `.z64`, `.n64` or `.v64`); the build is detected automatically and the
  output keeps the input's byte order.

New key codes of the patched firmware:

| Key                         | 09:54 Mar 27                       | 15:56 Apr 4                        |
|-----------------------------|------------------------------------|------------------------------------|
| Mario World 64 & Others (6102) | `271F6120 DEEA5464 78 80180000` | `1A965CD6 59FEF6D9 76 80180000` |
| Diddy, 1080, Banjo, Griffey (6103) | `0E1E863E E4168444 76 80201000` | `4257BA63 B75DC4E2 0C 80201000` |
| Zelda (6105)                | `49BAD096 6F07D047 D1 80190000` | `7CF3A93C DDC550F0 F3 80190000` |
| Yoshis, F-Zero, C'World (6106) | `1AAC5E5E 7199FF24 76 80200400` | `FE2AF47C B2DD5D51 94 80200400` |
| Aleck64 (CIC-5101)          | `6BA67BBA C5F16A96 44 80201000` | `390F92E7 4A9D25CD 7B 80201000` |

(CRC1 CRC2, check digit, entry point.) The values depend only on the firmware
code, so they are the same for every dump of the same build.

## Caveats

* **Flashing a modified firmware can brick the GameShark.** Keep a dump of the
  original and make sure you can reflash it with an external programmer.
* Old key codes no longer work with the patched firmware, and vice versa.
* Remove any `DE`/`F1` codes previously used as a workaround for Aleck64
  games.
* The loader stub is placed 0x300 bytes below the stack pointer; this assumes
  the menu's stack is less than about 6 KB deep when a game is started.
* Tested in a CPU emulator (launch path, copy length, register values); test
  on real hardware at your own risk.

## License

MIT, see [LICENSE](LICENSE). LZARI is based on the public-domain LZARI by
Haruhiko Okumura.
