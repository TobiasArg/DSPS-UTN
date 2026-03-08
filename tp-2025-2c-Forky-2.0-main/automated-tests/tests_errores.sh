#!/bin/bash

# ============================================================================
# SUITE COMPLETA DE TESTS DE MANEJO DE ERRORES
# ============================================================================
# Tests del nuevo código OP_WORKER_QUERY_ERROR
# ============================================================================

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

echo -e "${MAGENTA}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${MAGENTA}║                                                            ║${NC}"
echo -e "${MAGENTA}║        SUITE DE TESTS - MANEJO DE ERRORES                  ║${NC}"
echo -e "${MAGENTA}║                                                            ║${NC}"
echo -e "${MAGENTA}╚════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${CYAN}📋 Tests disponibles:${NC}"
echo -e "   1. ${YELLOW}test_file_existente.sh${NC}      - Crear archivo que ya existe"
echo -e "   2. ${YELLOW}test_escritura_commited.sh${NC} - Escribir en archivo commiteado"
echo -e "   3. ${YELLOW}test_lectura_limite.sh${NC}     - Leer fuera del límite"
echo -e "   4. ${YELLOW}test_tag_existente.sh${NC}      - Crear tag que ya existe"

echo -e "\n${BLUE}════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}              PREREQUISITOS                                 ${NC}"
echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"

echo -e "\n${YELLOW}⚠️  Antes de ejecutar los tests, asegúrate de tener:${NC}"
echo -e "   1. Master ejecutándose (ALGORITMO_PLANIFICACION=FIFO)"
echo -e "   2. Storage ejecutándose (FRESH_START=FALSE para tests 3 y 4)"
echo -e "   3. Worker ejecutándose con WorkerTestErrores.config"
echo -e "   4. Código OP_WORKER_QUERY_ERROR compilado"

echo -e "\n${CYAN}🔧 Configuraciones requeridas:${NC}"
echo -e "   Master.config:"
echo -e "      ALGORITMO_PLANIFICACION=FIFO"
echo -e "      TIEMPO_AGING=0"
echo -e ""
echo -e "   WorkerTestErrores.config:"
echo -e "      TAM_MEMORIA=256"
echo -e "      RETARDO_MEMORIA=250"
echo -e "      ALGORITMO_REEMPLAZO=LRU"

echo -e "\n${BLUE}════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}            EJECUCIÓN DE TESTS                              ${NC}"
echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"

echo -e "\n${YELLOW}Selecciona el modo de ejecución:${NC}"
echo -e "   ${CYAN}1${NC} - Ejecutar todos los tests automáticamente"
echo -e "   ${CYAN}2${NC} - Ejecutar tests individualmente (recomendado)"
echo -e "   ${CYAN}3${NC} - Ver instrucciones detalladas"
echo -e "   ${CYAN}q${NC} - Salir"

read -p "Opción: " opcion

case $opcion in
    1)
        echo -e "\n${GREEN}🚀 Ejecutando todos los tests...${NC}\n"
        
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}TEST 1: Archivo existente (1ª ejecución - debe pasar)${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ./test_file_existente.sh 0
        sleep 2
        
        echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}TEST 2: Escritura en archivo commiteado${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ./test_escritura_commited.sh 0
        sleep 2
        
        echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}TEST 3: Lectura fuera del límite${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ./test_lectura_limite.sh 0
        sleep 2
        
        echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}TEST 4: Tag existente${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ./test_tag_existente.sh 0
        sleep 2
        
        echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}TEST 5: Archivo existente (2ª ejecución - debe fallar)${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ./test_file_existente.sh 0
        
        echo -e "\n${GREEN}✅ Todos los tests ejecutados${NC}"
        ;;
        
    2)
        echo -e "\n${YELLOW}Ejecuta manualmente cada test:${NC}"
        echo -e "   ${CYAN}./test_file_existente.sh${NC}      (ejecutar 2 veces)"
        echo -e "   ${CYAN}./test_escritura_commited.sh${NC}"
        echo -e "   ${CYAN}./test_lectura_limite.sh${NC}"
        echo -e "   ${CYAN}./test_tag_existente.sh${NC}"
        ;;
        
    3)
        echo -e "\n${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${CYAN}║           INSTRUCCIONES DETALLADAS                        ║${NC}"
        echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
        
        echo -e "\n${YELLOW}PASO 1: Iniciar módulos${NC}"
        echo -e "   Terminal 1: ./master.sh"
        echo -e "   Terminal 2: ./storage.sh"
        echo -e "   Terminal 3: cd worker && ./bin/worker WorkerTestErrores.config 001"
        
        echo -e "\n${YELLOW}PASO 2: Ejecutar tests (Terminal 4)${NC}"
        echo -e "   ${GREEN}Test 1:${NC} ./test_file_existente.sh"
        echo -e "           ${CYAN}→ Debe pasar (crea archivo)${NC}"
        echo -e ""
        echo -e "   ${GREEN}Test 2:${NC} ./test_escritura_commited.sh"
        echo -e "           ${CYAN}→ Debe fallar en WRITE después de COMMIT${NC}"
        echo -e ""
        echo -e "   ${GREEN}Test 3:${NC} ./test_lectura_limite.sh"
        echo -e "           ${CYAN}→ Debe fallar (lectura fuera del límite)${NC}"
        echo -e ""
        echo -e "   ${GREEN}Test 4:${NC} ./test_tag_existente.sh"
        echo -e "           ${CYAN}→ Debe fallar en el tercer TAG (tag duplicado)${NC}"
        echo -e ""
        echo -e "   ${GREEN}Test 5:${NC} ./test_file_existente.sh (segunda vez)"
        echo -e "           ${CYAN}→ Debe fallar (archivo ya existe)${NC}"
        
        echo -e "\n${YELLOW}QUÉ OBSERVAR:${NC}"
        echo -e "   ${BLUE}En Worker:${NC} Detección de errores de Storage"
        echo -e "   ${BLUE}En Master:${NC} Recepción de OP_WORKER_QUERY_ERROR"
        echo -e "   ${BLUE}En Query Control:${NC} Mensaje de error apropiado"
        
        echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        ;;
        
    q|Q)
        echo -e "${YELLOW}Saliendo...${NC}"
        exit 0
        ;;
        
    *)
        echo -e "${RED}Opción inválida${NC}"
        exit 1
        ;;
esac

echo -e "\n${MAGENTA}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${MAGENTA}║                                                            ║${NC}"
echo -e "${MAGENTA}║              SUITE DE TESTS COMPLETADA                     ║${NC}"
echo -e "${MAGENTA}║                                                            ║${NC}"
echo -e "${MAGENTA}╚════════════════════════════════════════════════════════════╝${NC}"
