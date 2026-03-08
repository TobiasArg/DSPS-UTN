#!/bin/bash

# Script para configurar IPs y puertos en todos los archivos de configuración
# Útil para cambiar rápidamente entre entornos (localhost, laboratorio, VMs, etc.)

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}  CONFIGURADOR DE IPS Y PUERTOS DEL SISTEMA   ${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

# Función para mostrar configuración actual
mostrar_config_actual() {
    echo -e "${CYAN}📋 Configuración actual detectada:${NC}"
    echo ""
    
    # Buscar IP_MASTER en algún config de worker
    if [ -f "worker/Worker1.config" ]; then
        CURRENT_IP_MASTER=$(grep "^IP_MASTER=" worker/Worker1.config | cut -d'=' -f2)
        CURRENT_PUERTO_MASTER=$(grep "^PUERTO_MASTER=" worker/Worker1.config | cut -d'=' -f2)
        CURRENT_IP_STORAGE=$(grep "^IP_STORAGE=" worker/Worker1.config | cut -d'=' -f2)
        CURRENT_PUERTO_STORAGE=$(grep "^PUERTO_STORAGE=" worker/Worker1.config | cut -d'=' -f2)
        
        echo -e "  Master:  ${YELLOW}$CURRENT_IP_MASTER:$CURRENT_PUERTO_MASTER${NC}"
        echo -e "  Storage: ${YELLOW}$CURRENT_IP_STORAGE:$CURRENT_PUERTO_STORAGE${NC}"
        echo ""
    fi
}

# Presets predefinidos
echo -e "${CYAN}🎯 Presets disponibles:${NC}"
echo -e "  ${YELLOW}1)${NC} Localhost (127.0.0.1) - Para desarrollo local"
echo -e "  ${YELLOW}2)${NC} Laboratorio - Mismo host, una sola IP"
echo -e "  ${YELLOW}3)${NC} Multi-VM - IPs diferentes para Master y Storage"
echo -e "  ${YELLOW}4)${NC} Personalizado - Ingresar todo manualmente"
echo -e "  ${YELLOW}5)${NC} Ver configuración actual"
echo ""

read -p "Selecciona una opción [1-5]: " opcion

case $opcion in
    1)
        # Localhost
        IP_MASTER="127.0.0.1"
        PUERTO_MASTER="9001"
        IP_STORAGE="127.0.0.1"
        PUERTO_STORAGE="9002"
        IP_ESCUCHA="127.0.0.1"
        echo -e "${GREEN}✓ Configuración Localhost seleccionada${NC}"
        ;;
    
    2)
        # Laboratorio - Mismo host
        echo ""
        echo -e "${YELLOW}Configuración para Laboratorio (mismo host):${NC}"
        read -p "IP del host (ej: 192.168.1.100): " HOST_IP
        IP_MASTER="$HOST_IP"
        PUERTO_MASTER="9001"
        IP_STORAGE="$HOST_IP"
        PUERTO_STORAGE="9002"
        IP_ESCUCHA="$HOST_IP"
        echo -e "${GREEN}✓ Configuración aplicada: $HOST_IP${NC}"
        ;;
    
    3)
        # Multi-VM
        echo ""
        echo -e "${YELLOW}Configuración Multi-VM (máquinas separadas):${NC}"
        echo ""
        read -p "IP del Master: " IP_MASTER
        read -p "Puerto del Master [9001]: " PUERTO_MASTER
        PUERTO_MASTER=${PUERTO_MASTER:-9001}
        
        read -p "IP del Storage: " IP_STORAGE
        read -p "Puerto del Storage [9002]: " PUERTO_STORAGE
        PUERTO_STORAGE=${PUERTO_STORAGE:-9002}
        
        # Para Master y Storage, detectar IP automáticamente
        IP_ESCUCHA=$(hostname -I | awk '{print $1}')
        
        echo ""
        echo -e "${GREEN}✓ Master: $IP_MASTER:$PUERTO_MASTER${NC}"
        echo -e "${GREEN}✓ Storage: $IP_STORAGE:$PUERTO_STORAGE${NC}"
        echo -e "${GREEN}✓ Master/Storage escucharán en: $IP_ESCUCHA${NC}"
        ;;
    
    4)
        # Personalizado
        echo ""
        echo -e "${YELLOW}Configuración Personalizada:${NC}"
        read -p "IP del Master: " IP_MASTER
        read -p "Puerto del Master [9001]: " PUERTO_MASTER
        PUERTO_MASTER=${PUERTO_MASTER:-9001}
        
        read -p "IP del Storage: " IP_STORAGE
        read -p "Puerto del Storage [9002]: " PUERTO_STORAGE
        PUERTO_STORAGE=${PUERTO_STORAGE:-9002}
        
        read -p "IP de escucha: " IP_ESCUCHA
        echo -e "${GREEN}✓ Configuración personalizada aplicada${NC}"
        ;;
    
    5)
        # Ver configuración actual
        mostrar_config_actual
        exit 0
        ;;
    
    *)
        echo -e "${RED}❌ Opción inválida${NC}"
        exit 1
        ;;
esac

echo ""
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${CYAN}📝 Configuración a aplicar:${NC}"
echo -e "  Master IP:        ${YELLOW}$IP_MASTER${NC}"
echo -e "  Master Puerto:    ${YELLOW}$PUERTO_MASTER${NC}"
echo -e "  Storage IP:       ${YELLOW}$IP_STORAGE${NC}"
echo -e "  Storage Puerto:   ${YELLOW}$PUERTO_STORAGE${NC}"
echo -e "  IP Escucha:       ${YELLOW}$IP_ESCUCHA${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo ""

read -p "¿Aplicar estos cambios? (s/N): " confirmar

if [[ ! "$confirmar" =~ ^[sS]$ ]]; then
    echo -e "${YELLOW}❌ Operación cancelada${NC}"
    exit 0
fi

echo ""
echo -e "${YELLOW}🔧 Aplicando cambios...${NC}"
echo ""

# Función para actualizar un archivo de configuración
actualizar_config() {
    local archivo=$1
    local tipo=$2
    
    if [ ! -f "$archivo" ]; then
        echo -e "${RED}  ⚠️  $archivo no encontrado${NC}"
        return
    fi
    
    case $tipo in
        "master")
            # Master solo escucha
            sed -i "s/^PUERTO_ESCUCHA=.*/PUERTO_ESCUCHA=$PUERTO_MASTER/" "$archivo"
            sed -i "s/^IP_ESCUCHA=.*/IP_ESCUCHA=$IP_ESCUCHA/" "$archivo"
            echo -e "${GREEN}  ✓ $archivo actualizado${NC}"
            ;;
        
        "storage")
            # Storage solo escucha
            sed -i "s/^PUERTO_ESCUCHA=.*/PUERTO_ESCUCHA=$PUERTO_STORAGE/" "$archivo"
            sed -i "s/^IP_ESCUCHA=.*/IP_ESCUCHA=$IP_ESCUCHA/" "$archivo"
            echo -e "${GREEN}  ✓ $archivo actualizado${NC}"
            ;;
        
        "worker")
            # Worker se conecta a Master y Storage
            sed -i "s/^IP_MASTER=.*/IP_MASTER=$IP_MASTER/" "$archivo"
            sed -i "s/^PUERTO_MASTER=.*/PUERTO_MASTER=$PUERTO_MASTER/" "$archivo"
            sed -i "s/^IP_STORAGE=.*/IP_STORAGE=$IP_STORAGE/" "$archivo"
            sed -i "s/^PUERTO_STORAGE=.*/PUERTO_STORAGE=$PUERTO_STORAGE/" "$archivo"
            echo -e "${GREEN}  ✓ $archivo actualizado${NC}"
            ;;
        
        "query")
            # Query Control se conecta a Master
            sed -i "s/^IP_MASTER=.*/IP_MASTER=$IP_MASTER/" "$archivo"
            sed -i "s/^PUERTO_MASTER=.*/PUERTO_MASTER=$PUERTO_MASTER/" "$archivo"
            echo -e "${GREEN}  ✓ $archivo actualizado${NC}"
            ;;
    esac
}

# Actualizar Master
echo -e "${CYAN}📦 Actualizando Master...${NC}"
actualizar_config "master/master.config" "master"

# Actualizar Storage
echo -e "${CYAN}📦 Actualizando Storage...${NC}"
actualizar_config "storage/storage.config" "storage"

# Actualizar Workers
echo -e "${CYAN}📦 Actualizando Workers...${NC}"
for worker_config in worker/Worker*.config worker/WorkerPrueba*.config; do
    if [ -f "$worker_config" ]; then
        actualizar_config "$worker_config" "worker"
    fi
done

# Actualizar Query Control
echo -e "${CYAN}📦 Actualizando Query Control...${NC}"
actualizar_config "query_control/query.config" "query"

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}✅ Configuración aplicada correctamente${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}💡 Próximos pasos:${NC}"
echo -e "  1. Recompilar si es necesario: ${YELLOW}./compilar_todo.sh${NC}"
echo -e "  2. Iniciar el sistema: ${YELLOW}./lanzar_sistema_tmux.sh${NC}"
echo ""
