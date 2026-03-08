#!/bin/bash

# Script maestro para ejecutar todas las pruebas en orden correcto
# 1. Primero ejecuta las queries FIFO para crear archivos en Storage
# 2. Después ejecuta los tests de errores de propagación

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

clear

echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                                                                    ║${NC}"
echo -e "${BLUE}║           TEST COMPLETO - SISTEMA DE PROPAGACIÓN DE ERRORES       ║${NC}"
echo -e "${BLUE}║                                                                    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${CYAN}📋 Plan de ejecución:${NC}"
echo -e "   ${MAGENTA}FASE 1:${NC} Queries FIFO (crear archivos en Storage)"
echo -e "   ${MAGENTA}FASE 2:${NC} Tests de propagación de errores"

echo -e "\n${YELLOW}⚠️  Requisitos previos:${NC}"
echo -e "   • Storage debe estar ejecutándose (puerto 9002)"
echo -e "   • Master debe estar ejecutándose (puerto 9001)"
echo -e "   • Al menos 1 Worker debe estar conectado"
echo -e "   • Storage con FRESH_START=FALSE para mantener archivos creados"

echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
read -p "Presiona ENTER para continuar o Ctrl+C para cancelar..."
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

# ==============================================================================
# FASE 1: QUERIES FIFO - Crear archivos en Storage
# ==============================================================================

echo -e "${MAGENTA}╔════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${MAGENTA}║                        FASE 1: QUERIES FIFO                        ║${NC}"
echo -e "${MAGENTA}║              (Crear archivos para tests posteriores)               ║${NC}"
echo -e "${MAGENTA}╚════════════════════════════════════════════════════════════════════╝${NC}\n"

FIFO_QUERIES=("FIFO_1" "FIFO_2" "FIFO_3" "FIFO_4")
FIFO_SUCCESS=0
FIFO_TOTAL=0

for query in "${FIFO_QUERIES[@]}"; do
    FIFO_TOTAL=$((FIFO_TOTAL + 1))
    echo -e "${CYAN}[${FIFO_TOTAL}/4]${NC} Ejecutando query: ${YELLOW}${query}${NC}"
    
    if ./query_control.sh "${query}" 3 > /tmp/test_${query}.log 2>&1; then
        echo -e "      ${GREEN}✓ ${query} completada exitosamente${NC}"
        FIFO_SUCCESS=$((FIFO_SUCCESS + 1))
    else
        echo -e "      ${RED}✗ ${query} falló (ver /tmp/test_${query}.log)${NC}"
    fi
    
    # Pausa breve entre queries
    sleep 1
done

echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${MAGENTA}Resumen FASE 1:${NC} ${GREEN}${FIFO_SUCCESS}/${FIFO_TOTAL}${NC} queries FIFO completadas"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

if [ $FIFO_SUCCESS -lt $FIFO_TOTAL ]; then
    echo -e "${YELLOW}⚠️  Advertencia: Algunas queries FIFO fallaron${NC}"
    echo -e "${YELLOW}   Los tests de errores pueden no funcionar correctamente${NC}\n"
    read -p "¿Continuar de todas formas? (s/N): " continuar
    if [[ ! "$continuar" =~ ^[sS]$ ]]; then
        echo -e "${RED}Abortado por el usuario${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}✓ Archivos creados en Storage${NC}"
echo -e "${CYAN}Esperando 3 segundos antes de iniciar tests de errores...${NC}\n"
sleep 3

# ==============================================================================
# FASE 2: TESTS DE PROPAGACIÓN DE ERRORES
# ==============================================================================

echo -e "${MAGENTA}╔════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${MAGENTA}║                 FASE 2: TESTS DE PROPAGACIÓN DE ERRORES           ║${NC}"
echo -e "${MAGENTA}╚════════════════════════════════════════════════════════════════════╝${NC}\n"

ERROR_TESTS=(
    "file_existente.sh:FILE_EXISTENTE:CREATE en archivo duplicado"
    "escritura_archivo_commited.sh:ESCRITURA_ARCHIVO_COMMITED:WRITE después de COMMIT"
    "lectura_fuera_del_limite.sh:LECTURA_FUERA_DEL_LIMITE:READ fuera del límite"
    "tag_existente.sh:TAG_EXISTENTE:TAG duplicado"
)

ERROR_SUCCESS=0
ERROR_TOTAL=0

for test_info in "${ERROR_TESTS[@]}"; do
    ERROR_TOTAL=$((ERROR_TOTAL + 1))
    
    IFS=':' read -r script query desc <<< "$test_info"
    
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}[${ERROR_TOTAL}/4]${NC} Test: ${YELLOW}${desc}${NC}"
    echo -e "      Query: ${YELLOW}${query}${NC}"
    echo -e "      Script: ${YELLOW}${script}${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
    
    if [ -x "./${script}" ]; then
        # Ejecutar el test y capturar la salida
        ./${script} > /tmp/test_${query}.log 2>&1
        exit_code=$?
        
        # Buscar "ERROR" en el log de Query Control
        if grep -q "ERROR" /tmp/test_${query}.log; then
            echo -e "${GREEN}✓ Test EXITOSO - Error propagado correctamente${NC}"
            ERROR_SUCCESS=$((ERROR_SUCCESS + 1))
        else
            echo -e "${RED}✗ Test FALLÓ - No se detectó propagación de error${NC}"
            echo -e "${YELLOW}   Ver detalles en: /tmp/test_${query}.log${NC}"
        fi
    else
        echo -e "${RED}✗ Script no encontrado o no ejecutable: ${script}${NC}"
    fi
    
    echo ""
    sleep 2
done

# ==============================================================================
# RESUMEN FINAL
# ==============================================================================

echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                         RESUMEN FINAL                              ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════╝${NC}\n"

echo -e "${MAGENTA}FASE 1 - Queries FIFO:${NC} ${GREEN}${FIFO_SUCCESS}/${FIFO_TOTAL}${NC} completadas"
echo -e "${MAGENTA}FASE 2 - Tests Errores:${NC} ${GREEN}${ERROR_SUCCESS}/${ERROR_TOTAL}${NC} exitosos\n"

TOTAL_SUCCESS=$((FIFO_SUCCESS + ERROR_SUCCESS))
TOTAL_TESTS=$((FIFO_TOTAL + ERROR_TOTAL))

if [ $ERROR_SUCCESS -eq $ERROR_TOTAL ]; then
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║                  ✓ TODOS LOS TESTS EXITOSOS ✓                      ║${NC}"
    echo -e "${GREEN}║         Sistema de propagación de errores funcionando OK          ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════╝${NC}\n"
    exit 0
else
    echo -e "${RED}╔════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║              ✗ ALGUNOS TESTS FALLARON ✗                           ║${NC}"
    echo -e "${RED}║   Revisa los logs en /tmp/test_*.log para más detalles            ║${NC}"
    echo -e "${RED}╚════════════════════════════════════════════════════════════════════╝${NC}\n"
    exit 1
fi
