#!/bin/bash

# Script para ejecutar Worker 3
# Parámetros: Worker3.config, Worker ID 003

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

CONFIG_FILE="Worker3.config"
WORKER_ID="3"

WORKER_DIR="/home/utnso/tp-2025-2c-Forky-2.0/worker"

if [ $# -eq 1 ]; then
    WORKER_ID=$1
elif [ $# -gt 1 ]; then
    echo -e "${RED}Uso: $0 [WORKER_ID]${NC}"
    echo -e "Por defecto usa: worker3.config y Worker ID 003"
    exit 1
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}         EJECUTANDO WORKER 3            ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Worker ID: ${YELLOW}$WORKER_ID${NC}"
echo -e "Configuración: ${YELLOW}$CONFIG_FILE${NC}"
echo -e "Directorio: ${YELLOW}$WORKER_DIR${NC}"
echo -e "${BLUE}=========================================${NC}"

cd "$WORKER_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $WORKER_DIR${NC}"
    exit 1
}

if [ ! -f "./bin/worker" ]; then
    echo -e "${RED}❌ Error: No existe ./bin/worker${NC}"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ No existe la config: $CONFIG_FILE${NC}"
    exit 1
fi

binary_size=$(ls -lh "./bin/worker" | awk '{print $5}')
config_size=$(ls -lh "$CONFIG_FILE" | awk '{print $5}')
echo -e "       Binario: $binary_size"
echo -e "       Config:  $config_size"

echo -e "\n${GREEN}🚀 Iniciando Worker 3...${NC}"
./bin/worker "$CONFIG_FILE" "$WORKER_ID"
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Worker 3 terminó correctamente${NC}"
elif [ $exit_code -eq 130 ]; then
    echo -e "${YELLOW}⚠️  Detenido por el usuario (Ctrl+C)${NC}"
else
    echo -e "${RED}❌ Error en Worker 3 (código $exit_code)${NC}"
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}      WORKER 3 FINALIZADO               ${NC}"
echo -e "${BLUE}=========================================${NC}"
exit $exit_code
