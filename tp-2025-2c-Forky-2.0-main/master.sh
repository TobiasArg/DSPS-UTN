#!/bin/bash

# Script para ejecutar Master (servidor principal del TP)
# Master no recibe parámetros según su implementación

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Directorio del módulo Master
MASTER_DIR="/home/utnso/tp-2025-2c-Forky-2.0/master"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}          EJECUTANDO MASTER             ${NC}"
echo -e "${BLUE}=========================================${NC}"


# Cambiar al directorio de Master
cd "$MASTER_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $MASTER_DIR${NC}"
    exit 1
}

# Verificar que existe el binario
if [ ! -f "./bin/master" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario ./bin/master${NC}"
    echo -e "${YELLOW}💡 Ejecuta primero desde el directorio raíz: ./compilar_todo.sh${NC}"
    exit 1
fi

# Verificar que existe el archivo de configuración
CONFIG_FILE="master.config"
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}⚠️  Advertencia: No se encontró $CONFIG_FILE${NC}"
    echo -e "${YELLOW}💡 Archivos disponibles en el directorio:${NC}"
    ls -la *.config 2>/dev/null || echo "  No hay archivos .config"
    echo -e "${CYAN}[INFO]${NC} Master puede usar configuración por defecto"
fi


# Ejecutar Master (no recibe parámetros según la implementación)
./bin/master

# Capturar el código de salida
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Master terminó correctamente${NC}"
elif [ $exit_code -eq 130 ]; then
    echo -e "${YELLOW}⚠️  Master detenido por usuario (Ctrl+C)${NC}"
else
    echo -e "${RED}❌ Master terminó con error (código: $exit_code)${NC}"
fi

echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}        MASTER FINALIZADO               ${NC}"
echo -e "${BLUE}=========================================${NC}"

exit $exit_code
