#!/bin/bash

# Script para ejecutar Worker con parámetros por defecto
# Parámetros: worker.config, Worker ID (001 por defecto)

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parámetros por defecto para Worker
CONFIG_FILE="Worker1.config"
WORKER_ID="1"

# Directorio del módulo Worker
WORKER_DIR="/home/utnso/tp-2025-2c-Forky-2.0/worker"

# Verificar si se pasaron argumentos opcionales
if [ $# -eq 2 ]; then
    CONFIG_FILE=$1
    WORKER_ID=$2
elif [ $# -eq 1 ]; then
    WORKER_ID=$1
elif [ $# -gt 2 ]; then
    echo -e "${RED}Uso: $0 [WORKER_ID] [archivo_config]${NC}"
    echo -e "O simplemente: $0 (usa Worker1.config y Worker ID=1 por defecto)"
    echo -e "Ejemplo: $0 2"
    echo -e "Ejemplo: $0 3 worker_custom.config"
    exit 1
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}         EJECUTANDO WORKER              ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Módulo: ${YELLOW}Worker (Procesador de tareas)${NC}"
echo -e "Función: ${YELLOW}Ejecuta tareas asignadas por Master${NC}"
echo -e "Worker ID: ${YELLOW}$WORKER_ID${NC}"
echo -e "Configuración: ${YELLOW}$CONFIG_FILE${NC}"
echo -e "Directorio: ${YELLOW}$WORKER_DIR${NC}"
echo -e "${BLUE}=========================================${NC}"

# Cambiar al directorio de Worker
cd "$WORKER_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder al directorio $WORKER_DIR${NC}"
    exit 1
}

# Verificar que existe el binario
if [ ! -f "./bin/worker" ]; then
    echo -e "${RED}❌ Error: No se encontró el binario ./bin/worker${NC}"
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

# Mostrar información de los archivos
echo -e "${BLUE}[INFO]${NC} Verificando archivos..."
binary_size=$(ls -lh "./bin/worker" | awk '{print $5}')
config_size=$(ls -lh "$CONFIG_FILE" | awk '{print $5}')
echo -e "       Binario: $binary_size bytes"
echo -e "       Config: $config_size bytes"

# Mostrar información del worker
echo -e "\n${CYAN}[WORKER]${NC} Información del módulo:"
echo -e "         Worker ID: $WORKER_ID"
echo -e "         Se conectará a Master para recibir tareas"
echo -e "         Se conectará a Storage para operaciones de archivos"
echo -e "         Ejecutará scripts y comandos asignados"

echo -e "\n${GREEN}🚀 Iniciando Worker $WORKER_ID...${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}[NOTA]${NC} Worker esperará tareas de Master"
echo -e "${CYAN}[CTRL+C]${NC} Para detener el Worker"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Ejecutar el Worker
./bin/worker "$CONFIG_FILE" "$WORKER_ID"

# Capturar el código de salida
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Worker $WORKER_ID terminó correctamente${NC}"
elif [ $exit_code -eq 130 ]; then
    echo -e "${YELLOW}⚠️  Worker $WORKER_ID detenido por usuario (Ctrl+C)${NC}"
else
    echo -e "${RED}❌ Worker $WORKER_ID terminó con error (código: $exit_code)${NC}"
fi

echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}    WORKER $WORKER_ID FINALIZADO        ${NC}"
echo -e "${BLUE}=========================================${NC}"

exit $exit_code
