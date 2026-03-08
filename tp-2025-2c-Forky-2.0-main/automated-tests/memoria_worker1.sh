#!/bin/bash

# Script para ejecutar Query Control con parámetros por defecto
# Parámetros: query.config, query_instrucciones.txt, prioridad 1

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parámetros por defecto para Query Control
CONFIG_FILE="query.config"
QUERY_FILE="MEMORIA_WORKER"
PRIORIDAD="0"

# Directorio del módulo Query Control
QUERY_CONTROL_DIR="/home/utnso/tp-2025-2c-Forky-2.0/query_control"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}       EJECUTANDO QUERY CONTROL        ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Archivo de configuración: ${YELLOW}$CONFIG_FILE${NC}"
echo -e "Archivo de queries: ${YELLOW}$QUERY_FILE${NC}"
echo -e "Prioridad: ${YELLOW}$PRIORIDAD${NC}"
echo -e "Directorio: ${YELLOW}$QUERY_CONTROL_DIR${NC}"
echo -e "${BLUE}=========================================${NC}"

# Cambiar al directorio de Query Control
cd "$QUERY_CONTROL_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $QUERY_CONTROL_DIR${NC}"
    exit 1
}

# Verificar que existe el binario
if [ ! -f "./bin/query_control" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario ./bin/query_control${NC}"
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

# Verificar que existe el archivo de queries
if [ ! -f "$QUERY_FILE" ]; then
    echo -e "${RED}❌ Error: No se encontró el archivo de queries: $QUERY_FILE${NC}"
    echo -e "${YELLOW}💡 Archivos disponibles en el directorio:${NC}"
    ls -la *.txt 2>/dev/null || echo "  No hay archivos .txt"
    exit 1
fi

# Mostrar información de los archivos
echo -e "${BLUE}[INFO]${NC} Verificando archivos..."
echo -e "       Config: $(ls -lh "$CONFIG_FILE" | awk '{print $5 " bytes"}')"
echo -e "       Queries: $(ls -lh "$QUERY_FILE" | awk '{print $5 " bytes"}')"

# Contar líneas en el archivo de queries
total_lines=$(wc -l < "$QUERY_FILE" 2>/dev/null || echo "0")
echo -e "       Queries: $total_lines líneas de comandos"

echo -e "\n${GREEN}🚀 Iniciando Query Control...${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Ejecutar Query Control con los parámetros por defecto
./bin/query_control "$CONFIG_FILE" "$QUERY_FILE" "$PRIORIDAD"

# Capturar el código de salida
exit_code=$?

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Query Control ejecutado correctamente${NC}"
else
    echo -e "${RED}❌ Query Control terminó con error (código: $exit_code)${NC}"
fi

echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}     QUERY CONTROL FINALIZADO          ${NC}"
echo -e "${BLUE}=========================================${NC}"

exit $exit_code
