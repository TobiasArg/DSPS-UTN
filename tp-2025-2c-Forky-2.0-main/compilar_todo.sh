#!/bin/bash

# Script para compilar todos los módulos del TP
# Orden: utils (dependencia común) -> master, query_control, storage, worker

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para mostrar el header
print_header() {
    echo -e "${BLUE}=============================================${NC}"
    echo -e "${BLUE}       COMPILANDO TODOS LOS MÓDULOS        ${NC}"
    echo -e "${BLUE}=============================================${NC}"
}

# Función para compilar un módulo
compile_module() {
    local module_name=$1
    local module_path=$2
    
    echo -e "\n${YELLOW}[INFO]${NC} Compilando módulo: ${BLUE}$module_name${NC}"
    echo -e "       Directorio: $module_path"
    
    cd "$module_path" || {
        echo -e "${RED}[ERROR]${NC} No se pudo acceder al directorio: $module_path"
        return 1
    }
    
    # Limpiar compilación anterior
    echo -e "       Ejecutando: make clean"
    if ! make clean > /dev/null 2>&1; then
        echo -e "${YELLOW}[WARNING]${NC} make clean falló en $module_name (puede ser normal si no hay archivos para limpiar)"
    fi
    
    # Compilar
    echo -e "       Ejecutando: make"
    if make > /dev/null 2>&1; then
        echo -e "${GREEN}[OK]${NC} Módulo $module_name compilado exitosamente"
        return 0
    else
        echo -e "${RED}[ERROR]${NC} Falló la compilación del módulo $module_name"
        echo -e "${RED}        Ejecutando make para ver errores:${NC}"
        make
        return 1
    fi
}

# Función principal
main() {
    print_header
    
    # Directorio base del TP
    TP_BASE="/home/utnso/tp-2025-2c-Forky-2.0"
    
    # Verificar que estemos en el directorio correcto
    if [ ! -d "$TP_BASE" ]; then
        echo -e "${RED}[ERROR]${NC} Directorio base no encontrado: $TP_BASE"
        exit 1
    fi
    
    cd "$TP_BASE" || exit 1
    
    # Array con los módulos en orden de compilación
    # utils primero porque es dependencia de los otros
    modules=(
        "utils:$TP_BASE/utils"
        "master:$TP_BASE/master"
        "query_control:$TP_BASE/query_control"
        "storage:$TP_BASE/storage"
        "worker:$TP_BASE/worker"
    )
    
    failed_modules=()
    successful_modules=()
    
    # Compilar cada módulo
    for module_info in "${modules[@]}"; do
        IFS=':' read -r module_name module_path <<< "$module_info"
        
        if compile_module "$module_name" "$module_path"; then
            successful_modules+=("$module_name")
        else
            failed_modules+=("$module_name")
        fi
    done
    
    # Resumen final
    echo -e "\n${BLUE}=============================================${NC}"
    echo -e "${BLUE}           RESUMEN DE COMPILACIÓN           ${NC}"
    echo -e "${BLUE}=============================================${NC}"
    
    if [ ${#successful_modules[@]} -gt 0 ]; then
        echo -e "${GREEN}[ÉXITO]${NC} Módulos compilados correctamente:"
        for module in "${successful_modules[@]}"; do
            echo -e "        ✓ $module"
        done
    fi
    
    if [ ${#failed_modules[@]} -gt 0 ]; then
        echo -e "${RED}[ERROR]${NC} Módulos que fallaron:"
        for module in "${failed_modules[@]}"; do
            echo -e "        ✗ $module"
        done
        echo -e "\n${RED}[INFO]${NC} Revisa los errores de compilación arriba"
        exit 1
    else
        echo -e "\n${GREEN}[ÉXITO]${NC} Todos los módulos compilaron correctamente!"
        echo -e "${GREEN}        El TP está listo para ejecutar${NC}"
    fi
    
    # Volver al directorio base
    cd "$TP_BASE"
}

# Verificar si se pasó el parámetro --help o -h
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Uso: $0 [opciones]"
    echo ""
    echo "Script para compilar todos los módulos del TP"
    echo ""
    echo "Opciones:"
    echo "  -h, --help    Mostrar esta ayuda"
    echo "  --verbose     Mostrar output detallado de make"
    echo ""
    echo "Módulos que se compilan:"
    echo "  1. utils (librería común)"
    echo "  2. master"
    echo "  3. query_control"
    echo "  4. storage"
    echo "  5. worker"
    exit 0
fi

# Ejecutar función principal
main "$@"
