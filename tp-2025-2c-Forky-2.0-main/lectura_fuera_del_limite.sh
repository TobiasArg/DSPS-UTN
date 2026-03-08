#!/bin/bash

# Script para TEST: LECTURA_FUERA_DEL_LIMITE
# Prueba leer fuera del límite del archivo

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

QUERY_CONTROL_DIR="/home/utnso/tp-2025-2c-Forky-2.0/query_control"
CONFIG_FILE="query.config"
QUERY_FILE="LECTURA_FUERA_DEL_LIMITE"
PRIORIDAD="1"  # Prioridad fija para test de errores

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}║          TEST: LECTURA FUERA DEL LÍMITE                    ║${NC}"
echo -e "${BLUE}║                                                            ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${CYAN}📋 Descripción del test:${NC}"
echo -e "   Este test intenta leer fuera del límite del archivo."
echo -e "   Requiere que primero se haya ejecutado el test:"
echo -e "   ${YELLOW}./test_escritura_commited.sh${NC}"
echo -e "   (que crea el archivo metroid:v1 con tamaño 512 bytes)"

echo -e "\n${YELLOW}🔧 Configuración:${NC}"
echo -e "   Config: ${CYAN}$CONFIG_FILE${NC}"
echo -e "   Query: ${CYAN}$QUERY_FILE${NC}"
echo -e "   Prioridad: ${CYAN}$PRIORIDAD${NC}"

echo -e "\n${YELLOW}📝 Instrucciones de la query:${NC}"
cat "$QUERY_CONTROL_DIR/$QUERY_FILE" | nl -ba -s '   ' | sed 's/^/   /'

echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}⚠️  Contexto:${NC}"
echo -e "   • Archivo: metroid:v1"
echo -e "   • Tamaño: 512 bytes (offset 0 a 511)"
echo -e "   • Intento: READ offset=544, size=4 bytes"
echo -e "   • Problema: 544 + 4 = 548 > 512 ${RED}(fuera del límite)${NC}"
echo -e ""
echo -e "${YELLOW}⚠️  Resultado esperado:${NC}"
echo -e "   • Storage detecta lectura fuera del límite"
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

echo -e "\n${YELLOW}💡 IMPORTANTE: Este test requiere que el archivo metroid:v1 exista.${NC}"
echo -e "${YELLOW}   Si no lo has creado, ejecuta primero:${NC}"
echo -e "${CYAN}   ./test_escritura_commited.sh${NC}\n"

read -p "Presiona ENTER para continuar con el test..."

echo -e "\n${GREEN}🚀 Ejecutando test...${NC}\n"

./bin/query_control "$CONFIG_FILE" "$QUERY_FILE" "$PRIORIDAD"

exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${YELLOW}⚠️  Test completado, pero debió generar un ERROR${NC}"
    echo -e "${YELLOW}   (La lectura fuera del límite debe fallar)${NC}"
else
    echo -e "${GREEN}✅ Test comportándose correctamente${NC}"
    echo -e "${GREEN}   Error detectado como se esperaba${NC}"
fi

echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"

exit $exit_code
