#!/bin/bash
#
# Makes a folder from which the ffemu programs of an MSYS2 build run on a
# Windows that has no MSYS2, Cygwin or Git Bash: the programs in bin/; the
# MSYS2 runtime DLL, its console helper and the terminal mintty in
# runtime/usr/bin/, so that runtime/ is the runtime's root, with its settings
# in runtime/etc/, the terminal types for ncurses in
# runtime/usr/share/terminfo/, mintty's themes in
# runtime/usr/share/mintty/themes/ and the licenses; and at the top a .cmd
# launcher for each program, which names the program and calls bin/launch.cmd
# for the rest, and README.txt; launch.cmd and README.txt are copies of the
# ones beside this script. A launcher runs its program in mintty on the
# folder's runtime, with the program's settings and log in bin/<program>/ and
# mintty's in bin/mintty/. Runs in the MSYS2 shell of the build.
#
# Usage: mk_dist.sh <out> <folder>
# <out> is the build's out/ directory; <folder> is emptied first.

set -u

# xterm-256color is what mintty is set to and what the runtime sets for a
# Windows console, cygwin what it sets for a legacy one, xterm what mintty
# sets by default.
readonly TERMS=(xterm-256color cygwin xterm)

fail() {
  echo "mk_dist.sh: $*" >&2
  exit 1
}

# Writes text $2 into file $1 with CRLF line ends, which cmd needs to find
# the labels of a batch file.
write_crlf() {
  printf '%s\n' "$2" | sed 's/$/\r/' > "$1" || fail "Cannot write $1"
}

# Writes the launcher of program $1 into folder $2, named after the program:
# it names the program and leaves the rest to bin/launch.cmd.
launcher() {
  local name="$1" dir="$2" text
  text="$(cat << 'EOF'
@echo off
setlocal
set "exe=@EXE@"
call "%~dp0bin\launch.cmd" %*
EOF
)"
  write_crlf "${dir}/${name}.cmd" "${text//@EXE@/${name}}"
}

# Writes mintty's settings into folder $1, where mintty also saves those
# changed from its menu: 16 colors at least, for the bright ones, the width
# that the TUI's layout is made for and the 50 rows of VGA's text mode,
# Braille from Segoe UI Symbol at the font's size, whose dots then fill the
# cell almost evenly, where Windows' fallback takes the same font shrunk to
# three quarters, and the VGA palette of the text mode whose look the TUI
# follows.
mintty_config() {
  mkdir -p "$1" && printf '%s\n' "Term=xterm-256color" "Columns=120" "Rows=50" \
      "FontChoice=|Braille Patterns:1" "Font1=Segoe UI Symbol" "ThemeFile=vga" \
      > "$1/config" || fail "Cannot write $1/config"
}

# Prints the MSYS2 DLLs that program $1 imports; ncurses is linked in.
msys_dlls() {
  objdump -p "$1" | awk '$1 == "DLL" && $3 ~ /^msys-/ { print $3 }'
}

main() {
  (( $# == 2 )) || fail "Usage: mk_dist.sh <out> <folder>"
  local out="$1" dir="$2" here p exe dll t hex
  [[ "$(uname -o)" == "Msys" ]] || fail "Runs in the MSYS2 shell only."
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" \
      || fail "Cannot locate the folder of this script."

  # The folder itself stays, since a window open on it would keep it.
  mkdir -p "${dir}" && find "${dir}" -mindepth 1 -delete \
      || fail "Cannot empty ${dir}"
  mkdir -p "${dir}/bin" "${dir}/runtime/usr/bin" "${dir}/runtime/etc" \
      "${dir}/runtime/usr/share/doc/Cygwin" \
      "${dir}/runtime/usr/share/mintty/themes" \
      "${dir}/runtime/usr/share/licenses/ncurses" \
      "${dir}/runtime/usr/share/licenses/mintty" || fail "Cannot make ${dir}"

  for p in shugart apple2; do
    exe="${out}/ffemu/${p}/ffemu-${p}.exe"
    cp "${exe}" "${dir}/bin/" || fail "Cannot copy ${exe}"
    launcher "ffemu-${p}" "${dir}"
  done
  cp /usr/bin/mintty.exe "${dir}/runtime/usr/bin/" \
      && cp /usr/share/mintty/themes/* \
          "${dir}/runtime/usr/share/mintty/themes/" \
      || fail "Cannot copy mintty"
  # Without its helper, the runtime gives a console program in mintty, which
  # has no console to pass on, a console of its own in a visible window.
  cp /usr/bin/cygwin-console-helper.exe "${dir}/runtime/usr/bin/" \
      || fail "Cannot copy cygwin-console-helper.exe"
  for exe in "${dir}"/bin/*.exe "${dir}/runtime/usr/bin/mintty.exe"; do
    for dll in $(msys_dlls "${exe}"); do
      cp -u "/usr/bin/${dll}" "${dir}/runtime/usr/bin/" \
          || fail "Cannot copy ${dll}"
    done
  done
  cp "${here}/launch.cmd" "${dir}/bin/" && cp "${here}/README.txt" "${dir}/" \
      || fail "Cannot copy launch.cmd and README.txt"
  mintty_config "${dir}/bin/mintty"

  # ncurses keeps a type in a directory named by its first letter in hex.
  for t in "${TERMS[@]}"; do
    hex="$(printf '%x' "'${t}")"
    mkdir -p "${dir}/runtime/usr/share/terminfo/${hex}" \
        && cp "/usr/share/terminfo/${hex}/${t}" \
            "${dir}/runtime/usr/share/terminfo/${hex}/" \
        || fail "Cannot copy terminal type ${t}"
  done

  cp "${here}/fstab" "${dir}/runtime/etc/" \
      && cp /usr/share/doc/Cygwin/COPYING \
          /usr/share/doc/Cygwin/CYGWIN_LICENSE \
          "${dir}/runtime/usr/share/doc/Cygwin/" \
      && cp /usr/share/licenses/ncurses/LICENSE \
          "${dir}/runtime/usr/share/licenses/ncurses/" \
      && cp /usr/share/licenses/mintty/* \
          "${dir}/runtime/usr/share/licenses/mintty/" \
      || fail "Cannot copy the settings and the licenses."
}

main "$@"
