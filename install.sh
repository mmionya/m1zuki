#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build" --parallel 2
ctest --test-dir "$project_dir/build" --output-on-failure
cmake --install "$project_dir/build" --prefix "$HOME/.local"
python3 - <<'PY'
import os
from pathlib import Path
target = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))) / 'applications/m1zuki.desktop'
target.parent.mkdir(parents=True, exist_ok=True)
binary = str(Path.home() / '.local/bin/m1zuki')
escaped = binary.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
entry = '[Desktop Entry]\nType=Application\nName=m1zuki\nComment=Номера ответов через OpenRouter\nExec="' + escaped + '" --settings\nIcon=m1zuki\nStartupWMClass=m1zuki\nTerminal=false\nCategories=Utility;\nStartupNotify=false\n'
target.write_text(entry, encoding='utf-8')
target.chmod(0o644)
# Keep existing pinned launchers working without a duplicate application-menu entry.
legacy_entry = target.with_name('mini-answer.desktop')
if legacy_entry.exists():
    legacy_entry.write_text(entry + 'NoDisplay=true\n', encoding='utf-8')
# Existing scripts may still use the old executable name.
legacy_binary = Path.home() / '.local/bin/mini-answer'
temporary_link = legacy_binary.with_name('.mini-answer-install-link')
temporary_link.unlink(missing_ok=True)
temporary_link.symlink_to('m1zuki')
temporary_link.replace(legacy_binary)
print('Установлено. Запустите m1zuki из меню приложений.')
PY
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
  kbuildsycoca6 --noincremental
fi
