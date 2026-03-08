#!/usr/bin/env bash
set -euo pipefail

MNT="${1:-.}"  # ejecutalo desde storage/ o pasale el path

# ---------- colores ----------
RED=$'\033[31m'; GRN=$'\033[32m'; YEL=$'\033[33m'; BLU=$'\033[34m'
MAG=$'\033[35m'; CYA=$'\033[36m'; BLD=$'\033[1m'; RST=$'\033[0m'

ok()   { echo "${GRN}✅ $*${RST}"; }
warn() { echo "${YEL}⚠️  $*${RST}"; }
bad()  { echo "${RED}❌ $*${RST}"; }
info() { echo "${CYA}ℹ️  $*${RST}"; }
hdr()  { echo "${BLD}${MAG}== $* ==${RST}"; }

# ---------- paths ----------
BITMAP="$MNT/bitmap.bin"
PHYS_DIR="$MNT/physical_blocks"
FILES_DIR="$MNT/files"

[ -f "$BITMAP" ] || { bad "No existe $BITMAP"; exit 1; }
[ -d "$PHYS_DIR" ] || { bad "No existe $PHYS_DIR"; exit 1; }
[ -d "$FILES_DIR" ] || { bad "No existe $FILES_DIR"; exit 1; }

hdr "CONFIG"
FS_SIZE=$(grep -E '^FS_SIZE=' "$MNT/superblock.config" 2>/dev/null | cut -d= -f2 || true)
BLOCK_SIZE=$(grep -E '^BLOCK_SIZE=' "$MNT/superblock.config" 2>/dev/null | cut -d= -f2 || true)
echo "FS_SIZE=${FS_SIZE:-?}  BLOCK_SIZE=${BLOCK_SIZE:-?}"
info "bitmap: $(stat -c 'size=%s bytes' "$BITMAP")"

hdr "CHECK 1: bitmap bits en 1 (xxd/bitcount)"
python3 - <<'PY' "$BITMAP"
import sys
path=sys.argv[1]
data=open(path,'rb').read()
ones=sum(bin(b).count("1") for b in data)
print(f"bitmap_bits_in_1={ones}  total_bits={len(data)*8}  total_bytes={len(data)}")
PY

hdr "CHECK 2: physical_blocks usados reales (nlink)"
python3 - <<'PY' "$PHYS_DIR"
import os,sys
phys=sys.argv[1]
used=[]
for name in os.listdir(phys):
    if not name.startswith("block") or not name.endswith(".dat"): 
        continue
    st=os.stat(os.path.join(phys,name))
    # regla TP típica: ocupado si tiene >=2 links (physical + al menos 1 logical)
    if st.st_nlink >= 2:
        used.append(name)
print(f"physical_blocks_used_by_nlink={len(used)} (nlink>=2)")
PY

hdr "CHECK 3: mismatches bitmap vs nlink (debería ser 0)"
python3 - <<'PY' "$BITMAP" "$PHYS_DIR"
import os,sys
bitmap_path=sys.argv[1]; phys=sys.argv[2]
b=open(bitmap_path,'rb').read()

def bit_test(i):
    byte=i//8
    bit=i%8
    # LSB_FIRST como commons/bitarray
    return (b[byte] >> bit) & 1

mismatch=[]
for name in os.listdir(phys):
    if not (name.startswith("block") and name.endswith(".dat")): 
        continue
    i=int(name[5:9])  # block0000.dat
    st=os.stat(os.path.join(phys,name))
    real = 1 if st.st_nlink >= 2 else 0
    bm = bit_test(i)
    if bm != real:
        mismatch.append((i,bm,real,st.st_nlink))
print("mismatches(bitmap!=nlink)=", len(mismatch))
if mismatch:
    print("sample:", mismatch[:20])
PY

hdr "CHECK 4: metadata blocks (unique) + phantom vs metadata"
python3 - <<'PY' "$FILES_DIR" "$BITMAP"
import os,sys,re,ast
files=sys.argv[1]; bitmap_path=sys.argv[2]
b=open(bitmap_path,'rb').read()

def bit_test(i):
    byte=i//8
    bit=i%8
    return (b[byte] >> bit) & 1

VACIO = 520  # ajustá si tu TP usa otro sentinel
met_blocks=set()

for root,dirs,fnames in os.walk(files):
    if "metadata.config" in fnames:
        path=os.path.join(root,"metadata.config")
        txt=open(path,errors="ignore").read()
        m=re.search(r'BLOCKS=\[(.*)\]', txt)
        if not m: 
            continue
        raw="["+m.group(1)+"]"
        try:
            arr=ast.literal_eval(raw)
        except Exception:
            continue
        for x in arr:
            if isinstance(x,int) and x != VACIO:
                met_blocks.add(x)

ones=set()
# total blocks = 4096 porque bitmap.bin en tu caso tiene 512 bytes
total_bits=len(b)*8
for i in range(total_bits):
    if bit_test(i)==1:
        ones.add(i)

phantom = sorted(list(ones - met_blocks))   # en bitmap=1 pero NO está en metadata
missing = sorted(list(met_blocks - ones))   # en metadata pero bitmap=0

print(f"metadata_unique_blocks={len(met_blocks)}")
if met_blocks:
    print("min,max:", min(met_blocks), max(met_blocks))
print(f"phantom_count(bitmap=1 not in metadata)={len(phantom)}")
print(f"missing_count(in metadata but bitmap=0)={len(missing)}")
print("phantom_sample:", phantom[:30])
print("missing_sample:", missing[:30])
PY

hdr "CHECK 5: resumen por archivo (*/V1)"
python3 - <<'PY' "$FILES_DIR" "$PHYS_DIR"
import os,sys,re,ast
files=sys.argv[1]; phys=sys.argv[2]
VACIO=520

def load_meta(path):
    txt=open(path,errors="ignore").read()
    tam=int(re.search(r'^TAMAÑO=(\d+)',txt, re.M).group(1))
    est=re.search(r'^ESTADO=(\w+)',txt, re.M).group(1)
    m=re.search(r'BLOCKS=\[(.*)\]', txt)
    arr=ast.literal_eval("["+m.group(1)+"]") if m else []
    return tam,est,arr

def read16(blocknum):
    p=os.path.join(phys, f"block{blocknum:04d}.dat")
    return open(p,'rb').read(16)

rows=[]
for root,dirs,fnames in os.walk(files):
    if root.endswith("/V1") and "metadata.config" in fnames:
        meta=os.path.join(root,"metadata.config")
        tam,est,arr=load_meta(meta)
        size_blocks=len(arr)
        bytes_calc=size_blocks*16
        # sample contenido: logical 0 y 4 si existen y no son VACIO
        sample=[]
        for logical in (0,4):
            if logical < len(arr) and arr[logical] != VACIO:
                physb=arr[logical]
                sample.append((logical,physb,read16(physb)))
        rows.append((root.replace(files+"/",""),tam,size_blocks,bytes_calc,est,sample))

rows.sort()
for path,tam,nb,bytes_calc,est,sample in rows:
    print(f"- {path} | TAMAÑO={tam} | len(BLOCKS)={nb} | bytes={bytes_calc} | ESTADO={est}")
    for logical,physb,data in sample:
        print(f"   logical {logical} -> phys {physb} | {data!r}")
PY

hdr "GRAFICO: ocupación del FS (bitmap)"
python3 - <<'PY' "$BITMAP"
import sys
b=open(sys.argv[1],'rb').read()
ones=sum(bin(x).count("1") for x in b)
total=len(b)*8
free=total-ones

# barra simple
def bar(x, total, width=50):
    n=int((x/total)*width) if total else 0
    return "█"*n + "░"*(width-n)

print(f"USED  {ones:4d} / {total}  {bar(ones,total)}")
print(f"FREE  {free:4d} / {total}  {bar(free,total)}")
PY

hdr "FIN"
