#!/bin/bash

# Script para ver el contenido de los bloques lógicos de un archivo en Storage
# Uso: ./ver_bloques.sh <NOMBRE_FILE> <TAG>

if [ $# -lt 2 ]; then
    echo "Error: Debes proporcionar el nombre del archivo y el tag"
    echo "Uso: $0 <NOMBRE_FILE> <TAG>"
    echo ""
    echo "Archivos disponibles en Storage:"
    ls -1 /home/utnso/tp-2025-2c-Forky-2.0/storage/files/ 2>/dev/null
    
    if [ $# -eq 1 ]; then
        echo ""
        echo "Tags disponibles para $1:"
        ls -1 /home/utnso/tp-2025-2c-Forky-2.0/storage/files/$1/ 2>/dev/null
    fi
    exit 1
fi

FILE_NAME="$1"
TAG_NAME="$2"
STORAGE_DIR="/home/utnso/tp-2025-2c-Forky-2.0/storage/files/$FILE_NAME"

if [ ! -d "$STORAGE_DIR" ]; then
    echo "Error: No existe el archivo '$FILE_NAME' en Storage"
    echo ""
    echo "Archivos disponibles:"
    ls -1 /home/utnso/tp-2025-2c-Forky-2.0/storage/files/ 2>/dev/null
    exit 1
fi

BLOCKS_DIR="$STORAGE_DIR/$TAG_NAME/logical_blocks"

if [ ! -d "$BLOCKS_DIR" ]; then
    echo "Error: No existe el tag '$TAG_NAME' para el archivo '$FILE_NAME'"
    echo ""
    echo "Tags disponibles para $FILE_NAME:"
    ls -1 "$STORAGE_DIR/" 2>/dev/null
    exit 1
fi

echo "=========================================="
echo "  Archivo: $FILE_NAME"
echo "  Tag: $TAG_NAME"
echo "=========================================="
echo ""

# Mostrar cada bloque
if [ "$(ls -A $BLOCKS_DIR 2>/dev/null)" ]; then
    for block_file in "$BLOCKS_DIR"/*.dat; do
        if [ -f "$block_file" ]; then
            BLOCK_NUM=$(basename "$block_file" .dat)
            BLOCK_SIZE=$(stat -c%s "$block_file" 2>/dev/null)
            
            echo "[Bloque $BLOCK_NUM] ($BLOCK_SIZE bytes):"
            echo "┌─────────────────────────────────────┐"
            echo -n "│ "
            cat "$block_file"
            echo ""
            echo "└─────────────────────────────────────┘"
            echo ""
        fi
    done
else
    echo "No hay bloques lógicos en este tag"
fi

echo "=========================================="
echo "  Fin del contenido"
echo "=========================================="
