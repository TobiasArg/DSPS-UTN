#!/bin/bash

# Script para lanzar 25 instancias de cada Query de Aging en segundo plano
# Todas con prioridad 20 (total = 100 queries)

# Colores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuración
QUERY_CONTROL_DIR="/home/utnso/tp-2025-2c-Forky-2.0/query_control"
CONFIG_FILE="query.config"
PRIORIDAD=20
LOG_DIR="/tmp/aging_logs"

# Crear directorio de logs
mkdir -p "$LOG_DIR"

echo -e "${BLUE}==========================================${NC}"
echo -e "${BLUE}  LANZANDO 25 INSTANCIAS DE CADA AGING  ${NC}"
echo -e "${BLUE}==========================================${NC}"
echo -e "${YELLOW}Prioridad:${NC} $PRIORIDAD"
echo -e "${YELLOW}Total queries:${NC} 100 (4 AGING × 25 instancias)"
echo -e "${YELLOW}Logs:${NC} $LOG_DIR/"
echo -e "${BLUE}==========================================${NC}"
echo ""

# Cambiar al directorio de query_control
cd "$QUERY_CONTROL_DIR" || {
    echo -e "${RED}❌ Error: No se pudo acceder a $QUERY_CONTROL_DIR${NC}"
    exit 1
}

# Verificar binario
if [ ! -f "./bin/query_control" ]; then
    echo -e "${RED}❌ Error: No se encontró ./bin/query_control${NC}"
    exit 1
fi

# Contador de queries lanzadas
total_launched=0

# Lanzar instancias de cada AGING
for aging in AGING_1 AGING_2 AGING_3 AGING_4
do
    echo -e "${BLUE}>>>${NC} Lanzando 25 instancias de ${YELLOW}$aging${NC}..."
    
    # Verificar que existe el archivo
    if [ ! -f "$aging" ]; then
        echo -e "${RED}⚠️  Archivo $aging no encontrado, saltando...${NC}"
        continue
    fi
    
    # Lanzar 25 instancias
    for i in $(seq 1 25)
    do
        ./bin/query_control "$CONFIG_FILE" "$aging" "$PRIORIDAD" \
            > "$LOG_DIR/${aging}_${i}.log" 2>&1 &
        
        total_launched=$((total_launched + 1))
        
        # Pequeña pausa para no saturar
        sleep 0.01
    done
    
    echo -e "${GREEN}    ✓ 25 instancias de $aging lanzadas${NC}"
done

echo ""
echo -e "${GREEN}==========================================${NC}"
echo -e "${GREEN}✅ $total_launched queries lanzadas correctamente${NC}"
echo -e "${GREEN}==========================================${NC}"
echo -e "${YELLOW}Logs guardados en:${NC} $LOG_DIR/"
echo -e "${YELLOW}Ver procesos activos:${NC} ps aux | grep query_control"
echo -e "${YELLOW}Contar procesos:${NC} ps aux | grep query_control | wc -l"
echo ""
