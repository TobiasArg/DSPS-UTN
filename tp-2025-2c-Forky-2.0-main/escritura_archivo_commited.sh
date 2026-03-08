#!/bin/bash

# Script para TEST: ESCRITURA_ARCHIVO_COMMITED
# Prueba escritura en un archivo que ya fue commiteado

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

QUERY_CONTROL_DIR="/home/utnso/tp-2025-2c-Forky-2.0/query_control"
CONFIG_FILE="query.config"
QUERY_FILE="ESCRITURA_ARCHIVO_COMMITED"
PRIORIDAD="1"  # Prioridad fija para test de errores

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}║         TEST: ESCRITURA EN ARCHIVO COMMITEADO              ║${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${CYAN}📋 Descripción del test:${NC}"
echo -e "   Este test intenta escribir en un archivo que ya fue commiteado."
echo -e "   El comportamiento esperado es que Storage rechace la escritura"
echo -e "   después del COMMIT."

echo -e "\n${YELLOW}🔧 Configuración:${NC}"
echo -e "   Config: ${CYAN}$CONFIG_FILE${NC}"
echo -e "   Query: ${CYAN}$QUERY_FILE${NC}"
echo -e "   Prioridad: ${CYAN}$PRIORIDAD${NC}"

echo -e "\n${YELLOW}📝 Instrucciones de la query:${NC}"
cat "$QUERY_CONTROL_DIR/$QUERY_FILE" | nl -ba -s '   ' | sed 's/^/   /'

echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}⚠️  Resultado esperado:${NC}"
echo -e "   1. CREATE, TRUNCATE, WRITE y COMMIT → ${GREEN}✓ OK${NC}"
echo -e "   2. WRITE después de COMMIT → ${RED}✗ ERROR${NC}"
echo -e "   3. Worker detecta error de Storage"
echo -e "   4. Worker envía OP_WORKER_QUERY_ERROR al Master"
echo -e "   5. Master notifica error al Query Control"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

cd "$QUERY_CONTROL_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $QUERY_CONTROL_DIR${NC}"
    exit 1
}

if [ ! -f "./bin/query_control" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario ./bin/query_control${NC}"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ Error: No se encontró $CONFIG_FILE${NC}"
    exit 1
fi

if [ ! -f "$QUERY_FILE" ]; then
    echo -e "${RED}❌ Error: No se encontró $QUERY_FILE${NC}"
    exit 1
fi

echo -e "\n${GREEN}🚀 Ejecutando test...${NC}\n"

./bin/query_control "$CONFIG_FILE" "$QUERY_FILE" "$PRIORIDAD"

exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Test completado${NC}"
else
    echo -e "${RED}❌ Test terminó con error (código: $exit_code)${NC}"
fi

echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

exit $exit_code
