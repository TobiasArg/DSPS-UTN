#!/bin/bash

# Script para monitorear el uso de memoria de un proceso en tiempo real
# Detecta memory leaks observando el crecimiento constante de memoria

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
    echo -e "  ${YELLOW}./monitorear_memoria.sh master${NC}"
    echo -e "  ${YELLOW}./monitorear_memoria.sh storage${NC}"
    echo -e "  ${YELLOW}./monitorear_memoria.sh worker${NC}"
    echo -e "  ${YELLOW}./monitorear_memoria.sh query_control${NC}"
    echo ""
    exit 1
fi

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}    MONITOR DE MEMORIA - DETECCIÓN DE LEAKS    ${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}Buscando proceso: ${YELLOW}$MODULO${NC}"
echo ""

# Buscar el PID del proceso
PID=$(pgrep -f "bin/$MODULO" | head -1)

if [ -z "$PID" ]; then
    echo -e "${RED}❌ No se encontró el proceso $MODULO${NC}"
    echo -e "${YELLOW}⚠️  Asegurate de que el proceso esté corriendo${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Proceso encontrado - PID: $PID${NC}"
echo ""
echo -e "${CYAN}Monitoreando uso de memoria cada 2 segundos...${NC}"
echo -e "${YELLOW}(Ctrl+C para detener)${NC}"
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
printf "${CYAN}%-10s %-12s %-12s %-10s${NC}\n" "TIEMPO" "RSS (KB)" "VSZ (KB)" "%MEM"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"

INICIO=$(date +%s)
SAMPLES=()

# Función para limpiar al salir
cleanup() {
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo -e "${CYAN}📊 Análisis de memoria:${NC}"
    echo ""
    
    if [ ${#SAMPLES[@]} -gt 2 ]; then
        PRIMERA=${SAMPLES[0]}
        ULTIMA=${SAMPLES[-1]}
        CRECIMIENTO=$((ULTIMA - PRIMERA))
        PORCENTAJE=$(echo "scale=2; ($CRECIMIENTO * 100) / $PRIMERA" | bc)
        
        echo -e "${CYAN}  RSS Inicial:${NC}  ${YELLOW}${PRIMERA} KB${NC}"
        echo -e "${CYAN}  RSS Final:${NC}    ${YELLOW}${ULTIMA} KB${NC}"
        echo -e "${CYAN}  Crecimiento:${NC} ${YELLOW}${CRECIMIENTO} KB${NC} (${PORCENTAJE}%)"
        echo ""
        
        if [ $CRECIMIENTO -gt 1000 ]; then
            echo -e "${RED}⚠️  POSIBLE MEMORY LEAK DETECTADO${NC}"
            echo -e "${RED}   La memoria creció más de 1 MB${NC}"
        elif [ $CRECIMIENTO -gt 100 ]; then
            echo -e "${YELLOW}⚠️  Crecimiento moderado de memoria${NC}"
            echo -e "${YELLOW}   Vigilar si sigue creciendo${NC}"
        else
            echo -e "${GREEN}✓ Uso de memoria estable${NC}"
        fi
    fi
    
    echo ""
    exit 0
}

trap cleanup INT TERM

# Monitorear en loop
while true; do
    # Verificar que el proceso siga corriendo
    if ! kill -0 $PID 2>/dev/null; then
        echo ""
        echo -e "${RED}❌ El proceso terminó${NC}"
        break
    fi
    
    # Obtener datos de memoria
    STATS=$(ps -p $PID -o rss=,vsz=,%mem= 2>/dev/null)
    
    if [ -z "$STATS" ]; then
        echo -e "${RED}❌ No se pudo obtener info del proceso${NC}"
        break
    fi
    
    RSS=$(echo $STATS | awk '{print $1}')
    VSZ=$(echo $STATS | awk '{print $2}')
    MEM=$(echo $STATS | awk '{print $3}')
    
    # Guardar muestra
    SAMPLES+=($RSS)
    
    # Calcular tiempo transcurrido
    AHORA=$(date +%s)
    TIEMPO=$((AHORA - INICIO))
    
    # Formatear tiempo como MM:SS
    MINUTOS=$((TIEMPO / 60))
    SEGUNDOS=$((TIEMPO % 60))
    TIEMPO_FMT=$(printf "%02d:%02d" $MINUTOS $SEGUNDOS)
    
    # Mostrar datos
    printf "%-10s %-12s %-12s %-10s\n" "$TIEMPO_FMT" "$RSS" "$VSZ" "$MEM%"
    
    sleep 2
done

cleanup
