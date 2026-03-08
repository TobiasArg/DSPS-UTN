#!/bin/bash

# Script para TEST: TAG_EXISTENTE
# Prueba crear un tag que ya existe

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

QUERY_CONTROL_DIR="/home/utnso/tp-2025-2c-Forky-2.0/query_control"
CONFIG_FILE="query.config"
QUERY_FILE="TAG_EXISTENTE"
PRIORIDAD="1"  # Prioridad fija para test de errores

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}║                 TEST: TAG EXISTENTE                        ║${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${CYAN}📋 Descripción del test:${NC}"
echo -e "   Este test prueba el manejo de tags duplicados."
echo -e "   Requiere que primero se haya ejecutado el test:"
echo -e "   ${YELLOW}./test_file_existente.sh${NC}"
echo -e "   (que crea el archivo initial_file:BASE)"

echo -e "\n${YELLOW}🔧 Configuración:${NC}"
echo -e "   Config: ${CYAN}$CONFIG_FILE${NC}"
echo -e "   Query: ${CYAN}$QUERY_FILE${NC}"
echo -e "   Prioridad: ${CYAN}$PRIORIDAD${NC}"

echo -e "\n${YELLOW}📝 Instrucciones de la query:${NC}"
cat "$QUERY_CONTROL_DIR/$QUERY_FILE" | nl -ba -s '   ' | sed 's/^/   /'

echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}⚠️  Flujo del test:${NC}"
echo -e "   1. TAG initial_file:BASE → initial_file:V1 ${GREEN}✓ OK${NC}"
echo -e "   2. COMMIT initial_file:V1 ${GREEN}✓ OK${NC}"
echo -e "   3. TAG initial_file:V1 → initial_file:BASE ${RED}✗ ERROR${NC}"
echo -e "      (inicial_file:BASE ya existe)"
echo -e ""
echo -e "${YELLOW}⚠️  Resultado esperado:${NC}"
echo -e "   • Primeros dos comandos → ${GREEN}✓ OK${NC}"
echo -e "   • Tercer comando (TAG duplicado) → ${RED}✗ ERROR${NC}"
echo -e "   • Storage detecta que el tag destino ya existe"
echo -e "   • Storage envía ST_ERROR al Worker"
echo -e "   • Worker detecta error"
echo -e "   • Worker envía OP_WORKER_QUERY_ERROR al Master"
echo -e "   • Master notifica error al Query Control"
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

echo -e "\n${YELLOW}💡 IMPORTANTE: Este test requiere que el archivo initial_file:BASE exista.${NC}"
echo -e "${YELLOW}   Si no lo has creado, ejecuta primero:${NC}"
echo -e "${CYAN}   ./test_file_existente.sh${NC}\n"

read -p "Presiona ENTER para continuar con el test..."

echo -e "\n${GREEN}🚀 Ejecutando test...${NC}\n"

./bin/query_control "$CONFIG_FILE" "$QUERY_FILE" "$PRIORIDAD"

exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${YELLOW}⚠️  Test completado, pero debió generar un ERROR${NC}"
    echo -e "${YELLOW}   (El tercer TAG debe fallar porque el tag destino ya existe)${NC}"
else
    echo -e "${GREEN}✅ Test comportándose correctamente${NC}"
    echo -e "${GREEN}   Error detectado como se esperaba${NC}"
fi

echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

exit $exit_code
