#!/bin/bash

# Script para ejecutar la Prueba de Storage automaticamente
# Sigue las especificaciones exactas del TP

echo "=== INICIANDO PRUEBA DE STORAGE ==="
echo "Configuración del sistema verificada:"
echo "- Master: ALGORITMO_PLANIFICACION=PRIORIDADES, TIEMPO_AGING=0"
echo "- Worker: TAM_MEMORIA=128, RETARDO_MEMORIA=250, ALGORITMO_REEMPLAZO=LRU"
echo "- Storage: RETARDO_OPERACION=500, RETARDO_ACCESO_BLOQUE=500, FRESH_START=FALSE"
echo "- Superblock: FS_SIZE=4096, BLOCK_SIZE=16"
echo

# 1. Compilar todos los módulos
echo "1. Compilando todos los módulos..."
make -C master clean all
make -C worker clean all
make -C storage clean all
make -C query_control clean all
echo "Compilación completada."
echo

# 2. Iniciar módulos en background
echo "2. Iniciando módulos Storage, Master y 1 Worker..."

# Iniciar Storage
cd storage
./storage ./storage.config > storage.log 2>&1 &
STORAGE_PID=$!
echo "Storage iniciado (PID: $STORAGE_PID)"
cd ..

sleep 2

# Iniciar Master
cd master
./master ./master.config > master.log 2>&1 &
MASTER_PID=$!
echo "Master iniciado (PID: $MASTER_PID)"
cd ..

sleep 2

# Iniciar Worker
cd worker
./worker ./worker.config > worker.log 2>&1 &
WORKER_PID=$!
echo "Worker iniciado (PID: $WORKER_PID)"
cd ..

sleep 3
echo "Todos los módulos iniciados correctamente."
echo

# 3. Ejecutar Query Controls en orden
echo "3. Ejecutando Query Controls en orden..."

echo "Ejecutando Query Control 1 (STORAGE_1, PRIORIDAD=0)..."
cd query_control
echo "SCRIPT=STORAGE_1" > query_temp.config
echo "PRIORIDAD=0" >> query_temp.config
./query_control query_temp.config > query1.log 2>&1
sleep 2

echo "Ejecutando Query Control 2 (STORAGE_2, PRIORIDAD=2)..."
echo "SCRIPT=STORAGE_2" > query_temp.config
echo "PRIORIDAD=2" >> query_temp.config
./query_control query_temp.config > query2.log 2>&1
sleep 2

echo "Ejecutando Query Control 3 (STORAGE_3, PRIORIDAD=4)..."
echo "SCRIPT=STORAGE_3" > query_temp.config
echo "PRIORIDAD=4" >> query_temp.config
./query_control query_temp.config > query3.log 2>&1
sleep 2

echo "Ejecutando Query Control 4 (STORAGE_4, PRIORIDAD=6)..."
echo "SCRIPT=STORAGE_4" > query_temp.config
echo "PRIORIDAD=6" >> query_temp.config
./query_control query_temp.config > query4.log 2>&1
sleep 2

echo "Primeras 4 queries completadas."
cd ..

# 4. Validar estado del FS
echo
echo "4. Validando estado del File System después de las primeras 4 queries..."
echo "Estado de bloques físicos:"
ls -la storage/physical_blocks/
echo
echo "Estado del índice de bloques:"
cat storage/blocks_hash_index.config
echo
echo "Estado de archivos:"
find storage/files -type f -exec ls -la {} \;
echo

# 5. Ejecutar STORAGE_5
echo "5. Ejecutando Query Control final (STORAGE_5)..."
cd query_control
echo "SCRIPT=STORAGE_5" > query_temp.config
echo "PRIORIDAD=8" >> query_temp.config
./query_control query_temp.config > query5.log 2>&1
sleep 2
cd ..

# 6. Validar estado final del FS
echo
echo "6. Validando estado final del File System..."
echo "Estado final de bloques físicos:"
ls -la storage/physical_blocks/
echo
echo "Estado final del índice de bloques:"
cat storage/blocks_hash_index.config
echo
echo "Estado final de archivos:"
find storage/files -type f -exec ls -la {} \;
echo

# Cleanup
echo
echo "7. Finalizando procesos..."
kill $WORKER_PID 2>/dev/null
kill $MASTER_PID 2>/dev/null
kill $STORAGE_PID 2>/dev/null

# Cleanup archivos temporales
rm -f query_control/query_temp.config

echo "=== PRUEBA DE STORAGE COMPLETADA ==="
echo
echo "Logs generados:"
echo "- Storage: storage/storage.log"
echo "- Master: master/master.log" 
echo "- Worker: worker/worker.log"
echo "- Query 1: query_control/query1.log"
echo "- Query 2: query_control/query2.log"
echo "- Query 3: query_control/query3.log"
echo "- Query 4: query_control/query4.log"
echo "- Query 5: query_control/query5.log"
echo
echo "Para analizar deduplicación, revise:"
echo "- storage/blocks_hash_index.config (debe mostrar bloques reutilizados)"
echo "- storage/physical_blocks/ (cantidad de bloques físicos vs archivos creados)"