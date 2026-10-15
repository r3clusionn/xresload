# xresload

A replacement for `xrdb` in one small C file: it puts your `~/.Xresources` on the X server's root window, where programs read settings like `Xft.dpi` and `Xcursor.size`, and prints them back. It covers what an `~/.xinitrc` needs from xrdb: merge, load, query, remove. It talks to the X server through XCB directly and doesn't run the C preprocessor, so it needs no libXmu and no compiler at run time, and it is about 12 times faster.

**Status:** in daily use on an HP OmniBook 7 with Gentoo, Xorg and dwm, where it replaced xrdb (one package fewer).

![Terminal output on a black background. "cat ~/.Xresources" prints three lines: "Xft.dpi: 192", "Xcursor.theme: Adwaita" and "Xcursor.size: 48". "xresload && xresload -q" prints the same three resources as the X server now holds them, sorted by name with a tab after each colon: "Xcursor.size: 48", "Xcursor.theme: Adwaita", "Xft.dpi: 192". "xrdb -query", what xrdb itself reads back, prints the identical three lines.](docs/images/terminal.png)

## Features

- **Merge** a file into the current resources (`xresload`, like `xrdb -merge`): same names are replaced, new ones added, the rest kept.
- **Load** (replace everything, like `xrdb -load`), **query** (`-q`) and **remove** (`-r`).
- **The same property xrdb writes:** `RESOURCE_MANAGER` on the first screen's root window, entries sorted by name, `name:<tab>value`. `xrdb -query` reads back exactly what xresload wrote and the other way round, so the two can be mixed.
- **Small:** 17 KB, links only libxcb; 0.96 ms per merge against 11.7 ms for `xrdb -merge`.

## How to install

Requires a C compiler, make and libxcb (any X11 system has it).

```sh
git clone https://github.com/r3clusionn/xresload
cd xresload
make install            # to ~/.local/bin; PREFIX=/usr/local for system-wide
```

## How to use

In `~/.xinitrc`, where `xrdb -merge ~/.Xresources` was:

```sh
xresload            # merges ~/.Xresources
```

Other forms:

```sh
xresload file       # merge another file
xresload -l file    # replace all resources with the file
xresload -q         # print the current resources
xresload -r         # remove all resources
```

The file format is xrdb's without the preprocessor: `name: value` per line, `!` starts a comment, a backslash at the end of a line continues it. Lines starting with `#` (`#include`, `#define`, `#ifdef`) are skipped with a warning.

## How it works

- Connects with `xcb_connect`, reads `RESOURCE_MANAGER` from the first screen's root window with one `GetProperty` request, and parses it and the file into one table (a later value for the same name wins).
- Sorts the table by name and writes it back with `ChangeProperty`. A property larger than the server's maximum request is written in chunks (the first replaces, the rest append).
- Each write is a checked request, so an X error is reported and gives exit status 1.
- xrdb, by contrast, pipes the file through `cpp` (from gcc) on every run and then goes through Xlib's resource manager; that is where its 11 ms and 12 MB go.

## Measurements

On the laptop (Ryzen AI 9 HX 475, Xorg 21.1 at depth 30), merging the three-line `~/.Xresources`:

| | xresload | xrdb 1.2.3 |
|---|---|---|
| Time per run, including starting the process | 0.96 ms | 11.7 ms |
| Peak memory (including xrdb's cpp) | 1.9 MiB | 12.0 MiB |
| Binary | 17 KB | 39 KB, plus cpp at run time |
| Shared libraries | libxcb (+ libXau, libXdmcp) | libX11, libXmuu, libxcb (+ libXau, libXdmcp) |

## Verification

On the running X server:

- `xresload -q` output compared byte for byte with `xrdb -query`: identical.
- Merging the same `~/.Xresources` again: the property is byte-identical afterwards.
- A test file with a comment, a `#define`, a continued line and extra spaces around the colon: the comment was ignored, the `#define` skipped with the warning, `xresloadtest.long: a\` + ` b` became `a b`, `  Xft.dpi :  192  ` became `Xft.dpi:	192`, and `xrdb -query` showed the new entry.
- `xresload -l` with the saved original restored the property byte for byte.
- Bad arguments print the usage and exit with status 2.

## Limits

- No preprocessor: no `#include`, `#define` or `#ifdef` (they are skipped). Most `~/.Xresources` files don't use them.
- Only the global `RESOURCE_MANAGER` on the first screen; xrdb's per-screen `SCREEN_RESOURCES` (for multi-screen X setups, not multi-monitor) isn't supported.
- No `-edit`, `-backup` or symbol definitions (`-D`).

## License

MIT (see `LICENSE`).
