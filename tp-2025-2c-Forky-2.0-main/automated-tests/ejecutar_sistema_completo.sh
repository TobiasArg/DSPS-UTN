#!/bin/bash

# Script para ejecutar todos los módulos del TP en el orden correcto
# Orden: Master -> Storage -> Query Control -> Worker

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Configuración de delays (en segundos)
DELAY_BETWEEN_MODULES=3
FINAL_DELAY=2

# Directorio base del proyecto
PROJECT_DIR="/home/utnso/tp-2025-2c-Forky-2.0"

# Arrays para PIDs y nombres de módulos
declare -a MODULE_PIDS=()
declare -a MODULE_NAMES=()

# Función para limpiar procesos al salir
cleanup() {
    echo -e "\n\n${YELLOW}⚠️  Señal de interrupción recibida - Deteniendo todos los módulos...${NC}"
    
    for i in "${!MODULE_PIDS[@]}"; do
        local pid=${MODULE_PIDS[$i]}
        local name=${MODULE_NAMES[$i]}
        
        if kill -0 "$pid" 2>/dev/null; then
            echo -e "${YELLOW}🔄 Deteniendo ${name} (PID: $pid)...${NC}"
            kill -TERM "$pid" 2>/dev/null
            
            # Esperar un poco para terminación limpia
            sleep 2
            
            # Si aún está corriendo, forzar terminación
            if kill -0 "$pid" 2>/dev/null; then
                echo -e "${RED}⚡ Forzando terminación de ${name}...${NC}"
                kill -KILL "$pid" 2>/dev/null
            fi
        fi
    done
    
    echo -e "\n${BLUE}=========================================${NC}"
    echo -e "${BLUE}    TODOS LOS MÓDULOS DETENIDOS        ${NC}"
    echo -e "${BLUE}=========================================${NC}"
    exit 0
}

# Configurar traps para manejo de señales
trap cleanup SIGINT SIGTERM

# Función para iniciar un módulo
start_module() {
    local module_name="$1"
    local script_name="$2"
    local display_name="$3"
    
    echo -e "\n${MAGENTA}▶️  INICIANDO: ${display_name}${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    # Verificar que el script existe
    if [ ! -f "$PROJECT_DIR/$script_name" ]; then
        echo -e "${RED}❌ Error: No se encontró el script $script_name${NC}"
        return 1
    fi
    
    # Hacer el script ejecutable por si acaso
    chmod +x "$PROJECT_DIR/$script_name"
    
    # Ejecutar el módulo en background
    cd "$PROJECT_DIR" || exit 1
    ./"$script_name" &
    local pid=$!
    
    # Guardar PID y nombre para cleanup
    MODULE_PIDS+=("$pid")
    MODULE_NAMES+=("$display_name")
    
    echo -e "${GREEN}✅ ${display_name} iniciado correctamente (PID: $pid)${NC}"
    
    # Verificar que el proceso sigue corriendo después de un momento
    sleep 1
    if ! kill -0 "$pid" 2>/dev/null; then
        echo -e "${RED}❌ Error: ${display_name} se cerró inesperadamente${NC}"
        return 1
    fi
    
    return 0
}

# Función para mostrar estado de módulos
show_status() {
    echo -e "\n${CYAN}📊 ESTADO ACTUAL DE LOS MÓDULOS:${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    for i in "${!MODULE_PIDS[@]}"; do
        local pid=${MODULE_PIDS[$i]}
        local name=${MODULE_NAMES[$i]}
        
        if kill -0 "$pid" 2>/dev/null; then
            echo -e "${GREEN}✅ ${name} - Corriendo (PID: $pid)${NC}"
        else
            echo -e "${RED}❌ ${name} - Detenido (PID: $pid)${NC}"
        fi
    done
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# Función principal
main() {
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}    INICIANDO SISTEMA COMPLETO TP      ${NC}"
    echo -e "${BLUE}=========================================${NC}"
    echo -e "Orden de ejecución:"
    echo -e "${CYAN}  1. Master      (Servidor principal)${NC}"
    echo -e "${CYAN}  2. Storage     (Almacenamiento)${NC}"
    echo -e "${CYAN}  3. Query Control (Cliente de consultas)${NC}"
    echo -e "${CYAN}  4. Worker      (Procesador de tareas)${NC}"
    echo -e "${BLUE}=========================================${NC}"
    
    # Cambiar al directorio del proyecto
    cd "$PROJECT_DIR" || {
        echo -e "${RED}❌ Error: No se pudo acceder al directorio $PROJECT_DIR${NC}"
        exit 1
    }
    
    echo -e "\n${YELLOW}⏳ Iniciando secuencia de módulos...${NC}"
    
    # 1. Iniciar Master (servidor principal)
    if ! start_module "master" "master.sh" "MASTER"; then
        echo -e "${RED}❌ Error crítico: No se pudo iniciar Master${NC}"
        exit 1
    fi
    
    echo -e "\n${YELLOW}⏳ Esperando ${DELAY_BETWEEN_MODULES}s antes del siguiente módulo...${NC}"
    sleep $DELAY_BETWEEN_MODULES
    
    # 2. Iniciar Storage (almacenamiento)
    if ! start_module "storage" "storage.sh" "STORAGE"; then
        echo -e "${RED}❌ Error crítico: No se pudo iniciar Storage${NC}"
        cleanup
        exit 1
    fi
    
    echo -e "\n${YELLOW}⏳ Esperando ${DELAY_BETWEEN_MODULES}s antes del siguiente módulo...${NC}"
    sleep $DELAY_BETWEEN_MODULES
    
    # 3. Iniciar Query Control (cliente)
    if ! start_module "query_control" "query_control.sh" "QUERY CONTROL"; then
        echo -e "${YELLOW}⚠️  Query Control no se ejecutó correctamente (puede ser normal si es un cliente)${NC}"
    fi
    
    echo -e "\n${YELLOW}⏳ Esperando ${DELAY_BETWEEN_MODULES}s antes del siguiente módulo...${NC}"
    sleep $DELAY_BETWEEN_MODULES
    
    # 4. Iniciar Worker (procesador)
    if ! start_module "worker" "worker.sh" "WORKER"; then
        echo -e "${YELLOW}⚠️  Worker no se ejecutó correctamente${NC}"
    fi
    
    echo -e "\n${GREEN}🎉 ¡TODOS LOS MÓDULOS INICIADOS!${NC}"
    
    # Mostrar estado inicial
    sleep $FINAL_DELAY
    show_status
    
    echo -e "\n${CYAN}📋 INFORMACIÓN DEL SISTEMA:${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}• Master y Storage son servidores que permanecerán activos${NC}"
    echo -e "${CYAN}• Query Control puede ejecutarse y finalizar (cliente)${NC}"
    echo -e "${CYAN}• Worker se conectará a Master y Storage${NC}"
    echo -e "${CYAN}• Usa Ctrl+C para detener todos los módulos${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    # Loop principal - monitoreo
    echo -e "\n${GREEN}🔄 Sistema en funcionamiento - Presiona Ctrl+C para detener${NC}"
    
    while true; do
        sleep 10
        
        # Verificar si algún servidor crítico se cerró
        local master_running=false
        local storage_running=false
        
        for i in "${!MODULE_PIDS[@]}"; do
            local pid=${MODULE_PIDS[$i]}
            local name=${MODULE_NAMES[$i]}
            
            if kill -0 "$pid" 2>/dev/null; then
                if [[ "$name" == "MASTER" ]]; then
                    master_running=true
                fi
                if [[ "$name" == "STORAGE" ]]; then
                    storage_running=true
                fi
            fi
        done
        
        # Si algún servidor crítico se cerró, mostrar advertencia
        if [[ "$master_running" == false ]] || [[ "$storage_running" == false ]]; then
            echo -e "\n${YELLOW}⚠️  Advertencia: Algunos servidores críticos pueden haberse detenido${NC}"
            show_status
        fi
    done
}

# Verificar dependencias antes de iniciar
echo -e "${BLUE}🔍 Verificando scripts de módulos...${NC}"

required_scripts=("master.sh" "storage.sh" "query_control.sh" "worker.sh")
missing_scripts=()

for script in "${required_scripts[@]}"; do
    if [ ! -f "$PROJECT_DIR/$script" ]; then
        missing_scripts+=("$script")
    fi
done

if [ ${#missing_scripts[@]} -gt 0 ]; then
    echo -e "${RED}❌ Error: Faltan los siguientes scripts:${NC}"
    for script in "${missing_scripts[@]}"; do
        echo -e "   - $script"
    done
    echo -e "\n${YELLOW}💡 Asegúrate de haber creado todos los scripts individuales${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Todos los scripts encontrados${NC}"

# Ejecutar función principal
main
