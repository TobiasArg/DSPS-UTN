#!/bin/bash

# =============================================================================
# TEST STORAGE_1 - Query Control 3
# SCRIPT=STORAGE_3, PRIORIDAD=4
# =============================================================================

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Configuración
PROJECT_DIR="/home/utnso/tp-2025-2c-Forky-2.0"
QUERY_CONTROL_DIR="$PROJECT_DIR/query_control"
SCRIPT_NAME="STORAGE_3"
PRIORIDAD=4

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}    TEST STORAGE_1 - Query Control 3    ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "${CYAN}Script: ${SCRIPT_NAME}${NC}"
echo -e "${CYAN}Prioridad: ${PRIORIDAD}${NC}"
echo -e "${BLUE}=========================================${NC}"

# Verificar que existe el binario
if [ ! -f "$QUERY_CONTROL_DIR/bin/query_control" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario de query_control${NC}"
    echo -e "${YELLOW}Ejecuta 'make' en el directorio query_control primero${NC}"
    exit 1
fi

# Verificar que existe el script de query
if [ ! -f "$QUERY_CONTROL_DIR/${SCRIPT_NAME}" ]; then
    echo -e "${RED}❌ Error: No se encontró el script ${SCRIPT_NAME}${NC}"
    echo -e "${YELLOW}Verifica que exista en: $QUERY_CONTROL_DIR/${NC}"
    exit 1
fi

cd "$QUERY_CONTROL_DIR" || exit 1

echo -e "\n${MAGENTA}▶️  Ejecutando Query Control...${NC}"
./bin/query_control query.config "$SCRIPT_NAME" "$PRIORIDAD"
EXIT_CODE=$?

echo -e "\n${BLUE}=========================================${NC}"
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}✅ Query Control 3 (${SCRIPT_NAME}) finalizado exitosamente${NC}"
else
    echo -e "${RED}❌ Query Control 3 (${SCRIPT_NAME}) finalizó con errores (código: $EXIT_CODE)${NC}"
fi
echo -e "${BLUE}=========================================${NC}"

exit $EXIT_CODE
