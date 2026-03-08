#!/bin/bash

# Script para eliminar todos los archivos .log del proyecto

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}      LIMPIEZA DE ARCHIVOS LOG          ${NC}"
echo -e "${BLUE}=========================================${NC}"

# Buscar todos los archivos .log
echo -e "\n${YELLOW}🔍 Buscando archivos .log...${NC}"
log_files=$(find . -name "*.log" -type f 2>/dev/null)

if [ -z "$log_files" ]; then
    echo -e "${GREEN}✅ No se encontraron archivos .log${NC}"
    echo -e "${BLUE}=========================================${NC}"
    exit 0
fi

# Contar archivos
total=$(echo "$log_files" | wc -l)
echo -e "${YELLOW}📋 Se encontraron $total archivo(s) .log:${NC}"
echo "$log_files" | sed 's|^\./||'

# Confirmar eliminación
echo -e "\n${YELLOW}⚠️  ¿Deseas eliminar estos archivos? (s/N):${NC} "
read -r respuesta

if [[ ! "$respuesta" =~ ^[sS]$ ]]; then
    echo -e "${BLUE}❌ Operación cancelada${NC}"
    echo -e "${BLUE}=========================================${NC}"
    exit 0
fi

# Eliminar archivos
echo -e "\n${YELLOW}🗑️  Eliminando archivos...${NC}"
find . -name "*.log" -type f -exec rm -v {} \; | sed 's|removed ||' | sed 's|^\./||'

echo -e "\n${GREEN}✅ Archivos .log eliminados correctamente${NC}"
echo -e "${BLUE}=========================================${NC}"
