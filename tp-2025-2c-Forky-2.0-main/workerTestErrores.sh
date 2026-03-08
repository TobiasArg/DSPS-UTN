#!/bin/bash

# Script para ejecutar Worker 2 con worker2.config (CLOCK-M)
# Parámetros: worker2.config, Worker ID 002

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parámetros para Worker 2
CONFIG_FILE="WorkerTestErrores.config"
WORKER_ID="001"

# Directorio del módulo Worker
WORKER_DIR="/home/utnso/tp-2025-2c-Forky-2.0/worker"

# Verificar argumentos opcionales
if [ $# -eq 1 ]; then
    WORKER_ID=$1
elif [ $# -gt 1 ]; then
    echo -e "${RED}Uso: $0 [WORKER_ID]${NC}"
    echo -e "Por defecto usa: worker2.config y Worker ID 002"
    echo -e "Ejemplo: $0 W002"
    exit 1
fi

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}         EJECUTANDO WORKER 2            ${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Módulo: ${YELLOW}Worker 2 (Procesador de tareas)${NC}"
echo -e "Función: ${YELLOW}Ejecuta tareas asignadas por Master${NC}"
echo -e "Worker ID: ${YELLOW}$WORKER_ID${NC}"
echo -e "Configuración: ${YELLOW}$CONFIG_FILE (CLOCK-M)${NC}"
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
    exit 1
fi

# Mostrar información de los archivos
echo -e "${BLUE}[INFO]${NC} Verificando archivos..."
binary_size=$(ls -lh "./bin/worker" | awk '{print $5}')
config_size=$(ls -lh "$CONFIG_FILE" | awk '{print $5}')
echo -e "       Binario: $binary_size bytes"
echo -e "       Config: $config_size bytes"

# Mostrar información del worker
echo -e "\n${CYAN}[WorkerTestErrores]${NC} Información del módulo:"
echo -e "         Worker ID: $WORKER_ID"
echo -e "         Algoritmo de reemplazo: LRU"
echo -e "         Se conectará a Master para recibir tareas"
echo -e "         Se conectará a Storage para operaciones de archivos"

echo -e "\n${GREEN}🚀 Iniciando Worker 2...${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}[NOTA]${NC} Worker 2 esperará tareas de Master"
echo -e "${CYAN}[CTRL+C]${NC} Para detener el Worker"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Ejecutar el Worker 2
./bin/worker "$CONFIG_FILE" "$WORKER_ID"

# Capturar el código de salida
exit_code=$?

echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $exit_code -eq 0 ]; then
    echo -e "${GREEN}✅ Worker 2 terminó correctamente${NC}"
elif [ $exit_code -eq 130 ]; then
    echo -e "${YELLOW}⚠️  Worker 2 detenido por usuario (Ctrl+C)${NC}"
else
    echo -e "${RED}❌ Worker 2 terminó con error (código: $exit_code)${NC}"
fi

echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}      WORKER 2 FINALIZADO               ${NC}"
echo -e "${BLUE}=========================================${NC}"

exit $exit_code
