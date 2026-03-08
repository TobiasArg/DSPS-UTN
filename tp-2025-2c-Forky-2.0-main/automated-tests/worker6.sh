#!/bin/bash

# Script para ejecutar Worker 6
# Parámetros: Worker6.config, Worker ID 006

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

CONFIG_FILE="Worker6.config"
WORKER_ID="6"

WORKER_DIR="/home/utnso/tp-2025-2c-Forky-2.0/worker"

if [ $# -eq 1 ]; then
    WORKER_ID=$1
elif [ $# -gt 1 ]; then
    echo -e "${RED}Uso: $0 [WORKER_ID]${NC}"
    exit 1
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}         EJECUTANDO WORKER 6            ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Worker ID: ${YELLOW}$WORKER_ID${NC}"
echo -e "Config: ${YELLOW}$CONFIG_FILE${NC}"
echo -e "${BLUE}=========================================${NC}"

cd "$WORKER_DIR" || {
    echo -e "${RED}❌ No se pudo acceder a $WORKER_DIR${NC}"
    exit 1
}

if [ ! -f "./bin/worker" ]; then
    echo -e "${RED}❌ No existe ./bin/worker${NC}"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ No existe $CONFIG_FILE${NC}"
    exit 1
fi

echo -e "\n${GREEN}🚀 Iniciando Worker 6...${NC}"
./bin/worker "$CONFIG_FILE" "$WORKER_ID"
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Worker 6 terminó correctamente${NC}"
else
    echo -e "${RED}❌ Worker 6 terminó con error ($exit_code)${NC}"
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}      WORKER 6 FINALIZADO               ${NC}"
echo -e "${BLUE}=========================================${NC}"
exit $exit_code
