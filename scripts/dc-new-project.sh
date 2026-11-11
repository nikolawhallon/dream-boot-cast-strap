#!/usr/bin/env bash
# Create a new Simulant game project from the kit's template.
#
#   dc-new-project.sh <directory> ["Game Title"]
#
# The project gets its own small shell.nix that imports the kit's shell.nix, so it shares the
# kit's (already built) Dreamcast SDK and Simulant builds. Nothing is copied from sdk/.
set -euo pipefail

DEST="${1:?usage: $0 <directory> [\"Game Title\"]}"
KIT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="$(basename "$(realpath -m "$DEST")")"
TITLE="${2:-$NAME}"

if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
  echo "$DEST exists and is not empty" >&2
  exit 1
fi

mkdir -p "$DEST"
# Everything from the template except build output
tar -C "$KIT/template" --exclude=./build -cf - . | tar -C "$DEST" -xf -

# shell.nix pointing at this kit (absolute path, so the project can live anywhere)
cat > "$DEST/shell.nix" <<EOF
# Dev shell: everything comes from the Dreamcast kit at $KIT
import $KIT/shell.nix { }
EOF

# Title: window title / disc name. Disc titles are plain ASCII, so strip anything else.
DISC_TITLE="$(printf '%s' "$TITLE" | tr -cd 'A-Za-z0-9 _-' | cut -c1-30)"
sed -i "s|^TITLE ?= .*|TITLE ?= $DISC_TITLE|" "$DEST/Makefile"
sed -i "s|config.title = \".*\";|config.title = \"$TITLE\";|" "$DEST/sources/main.cpp"

echo "Created $DEST"
echo "Next:"
echo "  cd $DEST && nix-shell"
echo "  make run-pc     # Linux"
echo "  make run-dc     # Dreamcast (Flycast)"
