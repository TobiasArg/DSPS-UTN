#!/bin/bash

# =====================================================
# Script para lanzar 4 Query Controls FIFO en terminales
# Cada terminal ejecuta un archivo FIFO diferente
# =====================================================

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Directorio base
BASE_DIR="/home/utnso/tp-2025-2c-Forky-2.0"

echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}  LANZANDO 4 QUERY CONTROLS (FIFO)     ${NC}"
echo -e "${CYAN}=========================================${NC}"

# Detectar el emulador de terminal disponible
if command -v gnome-terminal &> /dev/null; then
    TERMINAL="gnome-terminal"
    echo -e "${GREEN}✓ Usando: gnome-terminal${NC}"
elif command -v xfce4-terminal &> /dev/null; then
    TERMINAL="xfce4-terminal"
    echo -e "${GREEN}✓ Usando: xfce4-terminal${NC}"
elif command -v konsole &> /dev/null; then
    TERMINAL="konsole"
    echo -e "${GREEN}✓ Usando: konsole${NC}"
elif command -v xterm &> /dev/null; then
    TERMINAL="xterm"
    echo -e "${GREEN}✓ Usando: xterm${NC}"
else
    echo -e "${RED}❌ Error: No se encontró ningún emulador de terminal compatible${NC}"
    echo -e "${YELLOW}💡 Instala: gnome-terminal, xfce4-terminal, konsole o xterm${NC}"
    exit 1
fi

# Función para lanzar terminal según el tipo
launch_terminal() {
    local title="$1"
    local command="$2"
    
    case "$TERMINAL" in
        "gnome-terminal")
            gnome-terminal --title="$title" --tab -- bash -c "$command; exec bash" &
            ;;
        "xfce4-terminal")
            xfce4-terminal --title="$title" -e "bash -c '$command; exec bash'" &
            ;;
        "konsole")
            konsole --new-tab -p tabtitle="$title" -e bash -c "$command; exec bash" &
            ;;
        "xterm")
            xterm -T "$title" -e bash -c "$command; exec bash" &
            ;;
    esac
}

# Verificar que existen los scripts
for i in 1 2 3 4; do
    if [ ! -f "$BASE_DIR/fifo_$i.sh" ]; then
        echo -e "${RED}❌ Error: No se encontró fifo_$i.sh${NC}"
        exit 1
    fi
done

# Lanzar FIFO_1 (Prioridad 4)
echo -e "${BLUE}[1/4]${NC} Lanzando ${YELLOW}FIFO_1${NC} (Prioridad 4)..."
launch_terminal "FIFO1" "cd $BASE_DIR && echo -e '\033[1;32m=== FIFO 1 (Prioridad 4) ===' && bash fifo_1.sh"
sleep 1

# Lanzar FIFO_2 (Prioridad 3)
echo -e "${BLUE}[2/4]${NC} Lanzando ${YELLOW}FIFO_2${NC} (Prioridad 3)..."
launch_terminal "FIFO2" "cd $BASE_DIR && echo -e '\033[1;33m=== FIFO 2 (Prioridad 3) ===' && bash fifo_2.sh"
sleep 1

# Lanzar FIFO_3 (Prioridad 2)
echo -e "${BLUE}[3/4]${NC} Lanzando ${YELLOW}FIFO_3${NC} (Prioridad 2)..."
launch_terminal "FIFO3" "cd $BASE_DIR && echo -e '\033[1;36m=== FIFO 3 (Prioridad 2) ===' && bash fifo_3.sh"
sleep 1

# Lanzar FIFO_4 (Prioridad 1)
echo -e "${BLUE}[4/4]${NC} Lanzando ${YELLOW}FIFO_4${NC} (Prioridad 1)..."
launch_terminal "FIFO4" "cd $BASE_DIR && echo -e '\033[1;35m=== FIFO 4 (Prioridad 1) ===' && bash fifo_4.sh"

echo -e "${CYAN}=========================================${NC}"
echo -e "${GREEN}✓ 4 Query Controls lanzados${NC}"
echo -e "${CYAN}=========================================${NC}"
echo -e "Terminales abiertas:"
echo -e "  ${YELLOW}1.${NC} FIFO1 (Prioridad 4)"
echo -e "  ${YELLOW}2.${NC} FIFO2 (Prioridad 3)"
echo -e "  ${YELLOW}3.${NC} FIFO3 (Prioridad 2)"
echo -e "  ${YELLOW}4.${NC} FIFO4 (Prioridad 1)"
echo -e "${CYAN}=========================================${NC}"
echo -e "${BLUE}💡 Las queries se ejecutarán según el orden FIFO configurado en Master${NC}"
echo -e "${CYAN}=========================================${NC}"
