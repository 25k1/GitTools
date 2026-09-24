# gittools

A small helper that replaces the parts of TortoiseGit I actually used:
see what a `git pull` brought in, browse a branch's log, switch branches, and read a file's diff inside a commit. just a few dialogs behind short git aliases.

## Why

TortoiseGit's shell extension started hanging and crashing Explorer on my machine, and I only ever used four things from it. So gittools is those four things and nothing else.

## Features

- **`git pl`** - runs `git pull` with its output streaming to your terminal, then opens the log dialog if commits arrived. Silent when already up to date.
- **`git lg`** - log dialog for any `git log` arguments (rev range, `--all`, `-n`, paths).
- **`git br`** - branch switcher with current marker, upstream and tip subject.
- **Log dialog** - commit list, message, and changed files with insertion and deletion counts.
- **Diff viewer** - select one or more files to read their diffs together. Find with `Ctrl+F`, and `Ctrl+Shift+E` jumps into your editor at the matching line of the real file. Added and removed lines play a sound as you arrow through them.
- **Status bar** - shows the running git command and whether it succeeded, failed or was cancelled.

## Build

### Windows

Requires Visual Studio (tested on 2022 and 2026) with the C++ Clang tools, CMake and Ninja.

```
build.bat
```

### Linux

Requires CMake, GCC 11+ or Clang 14+, pkg-config and the GTK 3 and zlib development packages. On Debian or Ubuntu:

```
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev zlib1g-dev
./build.sh
```

To stamp a version into the binary (`--version`, and the file properties on Windows):

```
build.bat Release clean -DGITTOOLS_VERSION=1.2.3
./build.sh Release clean -DGITTOOLS_VERSION=1.2.3
```

## Install

Download the binary from the
[releases page](https://github.com/25k1/GitTools/releases/latest), or build it
yourself as above: `gittools.exe` on Windows, `gittools-linux-x86_64` on Linux
(rename it to `gittools`, `chmod +x` it and put it on your `PATH`, for example
`~/.local/bin`; it needs GTK 3). Put it somewhere stable, then:

```
gittools install-alias
```

That registers `git pl`, `git lg` and `git br` in your global git config, pointing at the exe by full path. Re-run it if you move the binary;
`gittools uninstall-alias` removes them.

## Subcommands

| Command | What it does |
| --- | --- |
| `gittools pull-log [args]` | `git pull` + post-pull log dialog |
| `gittools log [args]` | log dialog for any `git log` args |
| `gittools log-range OLD NEW` | log dialog for `OLD..NEW` |
| `gittools branch` | branch switcher dialog |
| `gittools install-alias` / `uninstall-alias` | add or remove the aliases |
| `gittools --version`, `-v` | print the version |

## Configuration

Settings live in your global git config under `gittools`. File > Options and the Find dialog write them for you, or set them by hand:

| Key | Default | Meaning |
| --- | --- | --- |
| `gittools.editor` | auto-detect | Editor command. `%1` is the file path (appended if absent), `%L` the line number. On Windows Notepad++ is found automatically and gets `-n<line>`; on Linux common editors (VS Code, Sublime, gedit, Kate, vim, emacs and others) get the line number automatically, and without a setting `xdg-open` is used |
| `gittools.soundvolume` | `50` | Diff line sound volume, 0-100; `0` silences them |
| `gittools.audiodevice` | default device | Output device id for the diff line sounds (WASAPI on Windows, PulseAudio or ALSA on Linux) |
| `gittools.unloadfarcommits` | `false` | Drop commits far from view in huge logs and reload them from git when needed |
| `gittools.wraparound` | `false` | Whether find wraps past the end |
| `gittools.debug` | `false` | Show the Output pane with the full git transcript |

Quote the editor path only if it contains spaces:

## Donate

If you find this useful: you can buy me a cup of bear at <https://paypal.me/gozaltech>
