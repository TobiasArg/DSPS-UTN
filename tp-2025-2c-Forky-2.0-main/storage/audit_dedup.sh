#!/usr/bin/env bash
# =========================
# Audit dedup + hardlinks
# Uso:
#   ./audit_dedup.sh RESIDENT_EVIL 0
#   ./audit_dedup.sh OST_NFSMW V1
# =========================

set -u

FS="${1:-}"
TAG="${2:-}"

if [[ -z "${FS}" || -z "${TAG}" ]]; then
  echo "Uso: $0 <FS_NAME> <TAG>"
  echo "Ej:  $0 RESIDENT_EVIL 0"
  exit 1
fi

ROOT="files/${FS}/${TAG}"
LB_DIR="${ROOT}/logical_blocks"

# ---- colores (si la terminal soporta) ----
if [[ -t 1 ]]; then
  RED=$'\033[31m'; GRN=$'\033[32m'; YLW=$'\033[33m'; BLU=$'\033[34m'; CYA=$'\033[36m'; RST=$'\033[0m'; BLD=$'\033[1m'
else
  RED=""; GRN=""; YLW=""; BLU=""; CYA=""; RST=""; BLD=""
fi

say() { echo -e "${BLD}${CYA}==>${RST} $*"; }
ok()  { echo -e "${GRN}[✓]${RST} $*"; }
warn(){ echo -e "${YLW}[!]${RST} $*"; }
err() { echo -e "${RED}[x]${RST} $*"; }

BITMAP_FILE=""
[[ -f "bitmap.bin" ]] && BITMAP_FILE="bitmap.bin"
[[ -z "${BITMAP_FILE}" && -f "bitmap.dat" ]] && BITMAP_FILE="bitmap.dat"

# -------------------------
# Header
# -------------------------
echo -e "${BLD}${BLU}==============================${RST}"
echo -e "${BLD}${BLU} AUDIT DEDUP / HARDLINKS      ${RST}"
echo -e "${BLD}${BLU} FS=${FS}  TAG=${TAG}          ${RST}"
echo -e "${BLD}${BLU}==============================${RST}"
date
echo

# -------------------------
# Validaciones básicas
# -------------------------
if [[ ! -d "${ROOT}" ]]; then
  err "No existe ${ROOT}"
  exit 2
fi

if [[ ! -d "${LB_DIR}" ]]; then
  err "No existe ${LB_DIR} (no hay logical_blocks)"
  exit 3
fi

if [[ ! -d "physical_blocks" ]]; then
  err "No existe ./physical_blocks"
  exit 4
fi

ok "Estructura OK: ${ROOT}"
echo

# -------------------------
# Listado metadata / estructura
# -------------------------
say "Archivos relevantes (metadata + logical_blocks)"
find "${ROOT}" -maxdepth 2 -type f -print | sed 's/^/  - /'
echo

# -------------------------
# Map logical -> physical (inode)
# -------------------------
say "Mapeo LogicalBlock -> inode -> PhysicalBlock"

# Pre-cargar: inode -> physical filename
declare -A inode_to_phys
while IFS= read -r -d '' pb; do
  inode="$(stat -c %i "$pb")"
  base="$(basename "$pb")"
  inode_to_phys["$inode"]="$base"
done < <(find physical_blocks -type f -print0)

printf "%-55s  %-12s  %s\n" "LOGICAL_BLOCK" "INODE" "PHYSICAL_BLOCK"
printf "%-55s  %-12s  %s\n" "-----------" "-----" "--------------"

declare -A phys_seen_count
declare -A used_phys_blocks

found_any=0
while IFS= read -r -d '' lb; do
  found_any=1
  inode="$(stat -c %i "$lb")"
  phys="${inode_to_phys[$inode]:-NO_ENCONTRADO}"
  printf "%-55s  %-12s  %s\n" "$lb" "$inode" "$phys"

  if [[ "$phys" != "NO_ENCONTRADO" ]]; then
    used_phys_blocks["$phys"]=1
    phys_seen_count["$phys"]=$(( ${phys_seen_count["$phys"]:-0} + 1 ))
  fi
done < <(find "${LB_DIR}" -maxdepth 1 -type f -print0)

echo

if [[ "$found_any" -eq 0 ]]; then
  warn "No encontré archivos dentro de ${LB_DIR}"
  find "${LB_DIR}" -maxdepth 1 -type f -print | sed 's/^/  - /'
  echo
fi

# -------------------------
# Detección de dedup: mismo physical usado por varios logical
# -------------------------
say "Dedup detectado (mismo physical usado por varios logical)"
found_dedup=0
for phys in "${!phys_seen_count[@]}"; do
  if (( phys_seen_count["$phys"] > 1 )); then
    found_dedup=1
    warn "PHYSICAL ${phys} usado por ${phys_seen_count["$phys"]} logical blocks (dedup dentro del archivo)"
  fi
done

if (( found_dedup == 0 )); then
  ok "No se detectó dedup dentro de este FS:TAG (puede existir dedup global igualmente)"
fi
echo

# -------------------------
# Hardlinks globales (top)
# -------------------------
say "Top physical_blocks con más hardlinks (dedup global del FS)"
# %n = cantidad de links
find physical_blocks -type f -printf '%n %p\n' | sort -nr | head -n 30 | sed 's/^/  /'
echo

# -------------------------
# Mostrar detalle de los physical blocks usados por este archivo
# -------------------------
say "Detalle de physical blocks usados por ${FS}:${TAG}"
if (( ${#used_phys_blocks[@]} == 0 )); then
  warn "No se detectaron physical blocks usados por ${FS}:${TAG} (mapeo vacío o no hay logical_blocks)"
else
  for phys in "${!used_phys_blocks[@]}"; do
    pb="physical_blocks/${phys}"
    if [[ -f "$pb" ]]; then
      links="$(stat -c %h "$pb")"
      size="$(stat -c %s "$pb")"
      inode="$(stat -c %i "$pb")"
      echo -e "  ${BLD}${phys}${RST}  inode=${inode}  links=${links}  size=${size} bytes"
      echo "    primeros 16 bytes (xxd):"
      xxd -g 1 -l 16 "$pb" | sed 's/^/      /'
    else
      warn "  ${phys} no existe en physical_blocks"
    fi
  done
fi
echo

# -------------------------
# Bitmap checks para los physical blocks usados (si hay bitmap)
# -------------------------
if [[ -n "${BITMAP_FILE}" && ${#used_phys_blocks[@]} -gt 0 ]]; then
  say "Bitmap check (LSB_FIRST) para blocks usados (archivo: ${BITMAP_FILE})"
  python3 - <<'PY' "$BITMAP_FILE" "${!used_phys_blocks[@]}"
import sys, re
bitmap_path = sys.argv[1]
phys_names = sys.argv[2:]
b = open(bitmap_path,"rb").read()

def bit_lsb(n):
    byte = b[n//8]
    return (byte >> (n%8)) & 1, n//8, byte

print("  PHYSICAL_BLOCK       N      bit  byte_idx  raw")
print("  --------------     ----    ---  --------  ----")
for name in sorted(phys_names):
    m = re.search(r'block(\d+)\.dat$', name)
    if not m:
        continue
    n = int(m.group(1))
    bit, byte_idx, raw = bit_lsb(n)
    print(f"  {name:<14}  {n:>5}     {bit}     {byte_idx:>5}    0x{raw:02x}")
PY
  echo
elif [[ -z "${BITMAP_FILE}" ]]; then
  warn "No encontré bitmap.bin ni bitmap.dat (salteo chequeo de bitmap)"
  echo
fi

# -------------------------
# Resumen final
# -------------------------
say "Resumen"
logical_count="$(find "${LB_DIR}" -maxdepth 1 -type f | wc -l)"
echo "  - FS:TAG          = ${FS}:${TAG}"
echo "  - logical_blocks  = ${logical_count}"
echo "  - physical usados = ${#used_phys_blocks[@]}"
echo "  - dedup en archivo= $(for p in "${!phys_seen_count[@]}"; do (( phys_seen_count["$p"] > 1 )) && echo 1; done | wc -l)"
echo
ok "Audit terminado"
