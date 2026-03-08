#!/bin/bash

# Script para analizar memory leaks con Valgrind
# Uso: ./analizar_leaks.sh [master|storage|worker|query]

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

MODULO=$1

if [ -z "$MODULO" ]; then
    echo -e "${RED}❌ Falta especificar el módulo${NC}"
    echo ""
    echo -e "${CYAN}Uso:${NC}"
    echo -e "  ${YELLOW}./analizar_leaks.sh master${NC}"
    echo -e "  ${YELLOW}./analizar_leaks.sh storage${NC}"
    echo -e "  ${YELLOW}./analizar_leaks.sh worker${NC}"
    echo -e "  ${YELLOW}./analizar_leaks.sh query${NC}"
    echo ""
    echo -e "${CYAN}Nota:${NC} Para worker, analiza Worker1 por defecto"
    exit 1
fi

# Verificar que valgrind esté instalado
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}❌ Valgrind no está instalado${NC}"
    echo -e "${YELLOW}Instalalo con: sudo apt install valgrind${NC}"
    exit 1
fi

# Determinar el binario a ejecutar
case $MODULO in
    master)
        BINARY="master/bin/master"
        CONFIG="master/master.config"
        ;;
    storage)
        BINARY="storage/bin/storage"
        CONFIG="storage/storage.config"
        ;;
    worker)
        BINARY="worker/bin/worker"
        CONFIG="worker/Worker1.config"
        echo -e "${CYAN}ℹ️  Analizando Worker1 (podés cambiarlo editando el script)${NC}"
        ;;
    query)
        BINARY="query_control/bin/query_control"
        CONFIG="query_control/query.config"
        echo -e "${YELLOW}⚠️  Query Control requiere argumentos adicionales${NC}"
        echo -e "${YELLOW}    Ejecutalo manualmente si necesitas pasar un archivo de queries${NC}"
        ;;
    *)
        echo -e "${RED}❌ Módulo inválido: $MODULO${NC}"
        echo -e "${CYAN}Módulos válidos: master, storage, worker, query${NC}"
        exit 1
        ;;
esac

# Verificar que el binario exista
if [ ! -f "$BINARY" ]; then
    echo -e "${RED}❌ Binario no encontrado: $BINARY${NC}"
    echo -e "${YELLOW}Compilá primero con: make -C ${MODULO}${NC}"
    exit 1
fi

# Verificar que el config exista
if [ ! -f "$CONFIG" ]; then
    echo -e "${YELLOW}⚠️  Config no encontrado: $CONFIG${NC}"
fi

# Crear directorio para logs de valgrind
mkdir -p valgrind_logs

# Nombre del archivo de log
LOG_FILE="valgrind_logs/${MODULO}_$(date +%Y%m%d_%H%M%S).log"

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}      ANÁLISIS DE MEMORY LEAKS - VALGRIND      ${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}Módulo:${NC}    ${YELLOW}$MODULO${NC}"
echo -e "${CYAN}Binario:${NC}   ${YELLOW}$BINARY${NC}"
echo -e "${CYAN}Config:${NC}    ${YELLOW}$CONFIG${NC}"
echo -e "${CYAN}Log:${NC}       ${YELLOW}$LOG_FILE${NC}"
echo ""
echo -e "${YELLOW}⏳ Ejecutando Valgrind...${NC}"
echo -e "${YELLOW}   (Ctrl+C para detener)${NC}"
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

# Ejecutar con Valgrind
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$LOG_FILE" \
    "$BINARY" "$CONFIG"

echo ""
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}✅ Análisis completado${NC}"
echo ""
echo -e "${CYAN}📄 Log guardado en: ${YELLOW}$LOG_FILE${NC}"
echo ""
echo -e "${CYAN}📊 Resumen de leaks:${NC}"
echo ""

# Mostrar resumen de leaks
if grep -q "definitely lost" "$LOG_FILE"; then
    DEFINITELY_LOST=$(grep "definitely lost:" "$LOG_FILE" | tail -1)
    echo -e "${RED}  $DEFINITELY_LOST${NC}"
else
    echo -e "${GREEN}  ✓ No hay leaks definitivos${NC}"
fi

if grep -q "indirectly lost" "$LOG_FILE"; then
    INDIRECTLY_LOST=$(grep "indirectly lost:" "$LOG_FILE" | tail -1)
    echo -e "${YELLOW}  $INDIRECTLY_LOST${NC}"
fi

if grep -q "possibly lost" "$LOG_FILE"; then
    POSSIBLY_LOST=$(grep "possibly lost:" "$LOG_FILE" | tail -1)
    echo -e "${YELLOW}  $POSSIBLY_LOST${NC}"
fi

if grep -q "still reachable" "$LOG_FILE"; then
    STILL_REACHABLE=$(grep "still reachable:" "$LOG_FILE" | tail -1)
    echo -e "${CYAN}  $STILL_REACHABLE${NC}"
fi

echo ""
echo -e "${CYAN}Para ver el log completo:${NC}"
echo -e "  ${YELLOW}less $LOG_FILE${NC}"
echo -e "  ${YELLOW}cat $LOG_FILE | grep -A 10 'definitely lost'${NC}"
echo ""
