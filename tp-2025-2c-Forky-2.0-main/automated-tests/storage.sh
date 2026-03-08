#!/bin/bash

# Script para ejecutar Storage (almacenamiento del TP)
# Parámetros: storage.config

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parámetros por defecto para Storage
CONFIG_FILE="storage.config"

# Directorio del módulo Storage
STORAGE_DIR="/home/utnso/tp-2025-2c-Forky-2.0/storage"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}         EJECUTANDO STORAGE             ${NC}"
echo -e "${BLUE}=========================================${NC}"

# Cambiar al directorio de Storage
cd "$STORAGE_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $STORAGE_DIR${NC}"
    exit 1
}

# Verificar que existe el binario
if [ ! -f "./bin/storage" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario ./bin/storage${NC}"
    echo -e "${YELLOW}💡 Ejecuta primero desde el directorio raíz: ./compilar_todo.sh${NC}"
    exit 1
fi

# Verificar que existe el archivo de configuración
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ Error: No se encontró el archivo de configuración: $CONFIG_FILE${NC}"
    echo -e "${YELLOW}💡 Archivos disponibles en el directorio:${NC}"
    ls -la *.config 2>/dev/null || echo "  No hay archivos .config"
    exit 1
fi

# Verificar directorio de bloques (si existe)
if [ -d "bloques" ]; then
    block_count=$(find bloques -name "*.dat" 2>/dev/null | wc -l)
    echo -e "       Bloques existentes: $block_count archivos"
elif [ -d "blocks" ]; then
    block_count=$(find blocks -name "*.dat" 2>/dev/null | wc -l)
    echo -e "       Bloques existentes: $block_count archivos"
else
    echo -e "       Directorio de bloques: Se creará automáticamente"
fi


# Ejecutar Storage con su archivo de configuración
./bin/storage "$CONFIG_FILE"

# Capturar el código de salida
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Storage terminó correctamente${NC}"
elif [ $exit_code -eq 130 ]; then
    echo -e "${YELLOW}⚠️  Storage detenido por usuario (Ctrl+C)${NC}"
else
    echo -e "${RED}❌ Storage terminó con error (código: $exit_code)${NC}"
fi

echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}       STORAGE FINALIZADO               ${NC}"
echo -e "${BLUE}=========================================${NC}"

exit $exit_code
