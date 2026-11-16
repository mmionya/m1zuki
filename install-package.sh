#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != Linux || ! -x "$package_dir/bin/m1zuki" ]]; then
  printf '%s\n' 'Run this installer from an extracted Linux binary package.' >&2
  exit 1
fi
prefix="${M1ZUKI_INSTALL_PREFIX:-$HOME/.local}"
mkdir -p "$prefix/bin" "$prefix/share"
install -m 755 "$package_dir/bin/m1zuki" "$prefix/bin/m1zuki"
cp -a "$package_dir/share/." "$prefix/share/"
M1ZUKI_INSTALL_PREFIX="$prefix" python3 - <<'PY'
import os
from pathlib import Path
prefix = Path(os.environ['M1ZUKI_INSTALL_PREFIX']).resolve()
target = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))) / 'applications/m1zuki.desktop'
target.parent.mkdir(parents=True, exist_ok=True)
binary = str(prefix / 'bin/m1zuki')
escaped = binary.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
icon = str(prefix / 'share/icons/hicolor/scalable/apps/m1zuki.svg')
entry = '[Desktop Entry]\nType=Application\nName=m1zuki\nComment=Answers via OpenRouter\nExec="' + escaped + '" --settings\nIcon=' + icon + '\nStartupWMClass=m1zuki\nTerminal=false\nCategories=Utility;\nStartupNotify=false\n'
target.write_text(entry, encoding='utf-8')
target.chmod(0o644)
legacy_entry = target.with_name('mini-answer.desktop')
if legacy_entry.exists():
    legacy_entry.write_text(entry + 'NoDisplay=true\n', encoding='utf-8')
legacy = prefix / 'bin/mini-answer'
temporary = legacy.with_name('.mini-answer-install-link')
temporary.unlink(missing_ok=True)
temporary.symlink_to('m1zuki')
temporary.replace(legacy)
print('Installed:', binary)
PY
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 --noincremental
fi
