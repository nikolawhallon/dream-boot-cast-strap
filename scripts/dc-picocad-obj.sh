#!/usr/bin/env bash
# Prepare a picoCAD OBJ export for Simulant (and the Dreamcast).
#
#   dc-picocad-obj.sh <model.obj> <output directory>
#
# Copies <model>.obj, the .mtl it references and the texture that .mtl references into the
# output directory, fixing three problems on the way:
#
#   1. picoCAD writes MTL keys in lowercase (map_kd, kd, ...). Simulant's MTL parser is
#      case-sensitive and ignores them, so the model loads without its texture.
#      -> keys are written the standard way (map_Kd, Kd, Ka, Ks, Ns).
#   2. picoCAD writes "tr 1.0". Simulant reads Tr as "transparency", so 1.0 means fully
#      transparent and every material gets alpha blending (slow on the Dreamcast).
#      -> the line is dropped.
#   3. picoCAD faces are quads and other polygons. Simulant's OBJ loader draws every
#      non-triangle face as a separate draw call ("triangle fan"), which is very slow.
#      -> every face is split into triangles (a fan from its first corner).
set -euo pipefail

OBJ="${1:?usage: $0 <model.obj> <output directory>}"
OUT="${2:?usage: $0 <model.obj> <output directory>}"
SRC_DIR="$(dirname "$OBJ")"
mkdir -p "$OUT"

# OBJ: split faces into triangles, keep everything else
awk '/^f / { for(i = 3; i < NF; i++) print "f", $2, $i, $(i+1); next } { print }' \
    "$OBJ" > "$OUT/$(basename "$OBJ")"

# MTL(s) referenced by the OBJ
for mtl in $(awk '/^mtllib / { print $2 }' "$OBJ"); do
    awk '
        /^tr /     { next }                       # see 2. above
        /^ka /     { $1 = "Ka" }
        /^kd /     { $1 = "Kd" }
        /^ks /     { $1 = "Ks" }
        /^ns /     { $1 = "Ns" }
        /^map_kd / { $1 = "map_Kd" }
        { print }
    ' "$SRC_DIR/$mtl" > "$OUT/$mtl"

    # Textures referenced by the MTL
    for tex in $(awk 'tolower($1) == "map_kd" { print $2 }' "$SRC_DIR/$mtl"); do
        cp "$SRC_DIR/$tex" "$OUT/$tex"
    done
done

echo "Wrote $(ls "$OUT" | tr '\n' ' ')to $OUT"
