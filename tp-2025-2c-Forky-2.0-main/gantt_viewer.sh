#!/bin/bash

LOG="./master/master.log"
[ ! -f "$LOG" ] && echo "No existe $LOG" && exit 1

# Colores por Worker
WORKER_COLORS=(
    [1]="\033[33m"  # Amarillo
    [2]="\033[35m"  # Magenta
)
NC='\033[0m'

# Símbolos
RUN="█"
IO="▒"
FIN="✓"
ERR="✗"
EVICT="↻"

declare -A query_info        # q_id -> prioridad
declare -A query_worker      # q_id -> worker asignado
declare -A query_state       # q_id -> estado (arrived, running, io, done, error)
declare -A query_arrived     # q_id -> timestamp de llegada
declare -a queries           # Lista de queries vistas
declare -a finish_order      # Orden de finalización de queries

# Funciones auxiliares
get_time() {
    grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}:[0-9]{3}' <<< "$1"
}

# Primera pasada: detectar todas las queries que llegan
while IFS= read -r line; do
    ts=$(get_time "$line")
    [ -z "$ts" ] && continue
    
    # Usar grep sin regex para evitar problemas con UTF-8
    if grep -q "Se conecta un Query Control" <<< "$line" && grep -q "Id asignado:" <<< "$line"; then
        q=$(echo "$line" | grep -o "Id asignado: [0-9]*" | grep -o "[0-9]*")
        p=$(echo "$line" | grep -o "prioridad [0-9]*" | grep -o "[0-9]*")
        if [[ -z "${query_info[$q]}" ]]; then
            query_info["$q"]="$p"
            query_arrived["$q"]="$ts"
            query_state["$q"]="arrived"
            queries+=("$q")
        fi
    fi
done < "$LOG"

# Ordenar queries
IFS=$'\n' queries=($(sort -n <<<"${queries[*]}"))
unset IFS

# Imprimir encabezado
echo
echo -e "\033[36mGANTT DE QUERIES\033[0m"
printf "%-18s %-10s |" "Hora" "PENDIENTES"
for q in "${queries[@]}"; do
    printf " \033[34m$q\033[0m"
done
printf " | Eventos\n"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Segunda pasada: procesar eventos por timestamp
declare -A events_at_time
declare -A event_messages     # Guardar mensajes literales del log
declare -a all_timestamps

while IFS= read -r line; do
    ts=$(get_time "$line")
    [ -z "$ts" ] && continue
    
    # Se conecta un Query Control
    if grep -q "Se conecta un Query Control" <<< "$line" && grep -q "Id asignado:" <<< "$line"; then
        q=$(echo "$line" | grep -o "Id asignado: [0-9]*" | grep -o "[0-9]*")
        msg=$(echo "$line" | sed 's/.*## //')  # Extrae desde ## al final
        events_at_time["$ts"]+="ARRIVE:$q|"
        event_messages["$ts:ARRIVE:$q"]="$msg"
        all_timestamps+=("$ts")
    fi
    
    # Se envía la Query al Worker
    if grep -q "Se env.*la Query" <<< "$line" && grep -q "al Worker" <<< "$line"; then
        q=$(echo "$line" | grep -oE "Query [0-9]+" | grep -o "[0-9]*")
        w=$(echo "$line" | grep -oE "Worker [0-9]+" | grep -o "[0-9]*")
        msg=$(echo "$line" | sed 's/.*## //')
        events_at_time["$ts"]+="ASSIGN:$q:$w|"
        event_messages["$ts:ASSIGN:$q:$w"]="$msg"
        all_timestamps+=("$ts")
    fi
    
    # Se envía mensaje de lectura
    if grep -q "Se env.*mensaje de lectura" <<< "$line"; then
        q=$(echo "$line" | grep -oE "Query [0-9]+" | grep -o "[0-9]*")
        msg=$(echo "$line" | sed 's/.*## //')
        events_at_time["$ts"]+="IO:$q|"
        event_messages["$ts:IO:$q"]="$msg"
        all_timestamps+=("$ts")
    fi
    
    # Terminó la Query (error o éxito)
    if grep -q "Se termin.*la Query" <<< "$line"; then
        q=$(echo "$line" | grep -oE "Query [0-9]+" | grep -o "[0-9]*")
        msg=$(echo "$line" | sed 's/.*## //')
        if grep -q "ERROR" <<< "$line"; then
            events_at_time["$ts"]+="ERROR:$q|"
            event_messages["$ts:ERROR:$q"]="$msg"
        else
            events_at_time["$ts"]+="FINISH:$q|"
            event_messages["$ts:FINISH:$q"]="$msg"
        fi
        all_timestamps+=("$ts")
    fi
    
    # Desalojo (EVICT_ACK)
    if grep -q "EVICT_ACK" <<< "$line" && grep -q "Query [0-9]" <<< "$line"; then
        q=$(echo "$line" | grep -oE "Query [0-9]+" | grep -o "[0-9]*")
        msg=$(echo "$line" | sed 's/.*\[EVICT_ACK\] //')  # Extrae desde [EVICT_ACK]
        events_at_time["$ts"]+="EVICT:$q|"
        event_messages["$ts:EVICT:$q"]="$msg"
        all_timestamps+=("$ts")
    fi
done < "$LOG"

# Ordenar y deduplicar timestamps
all_timestamps=($(printf '%s\n' "${all_timestamps[@]}" | sort -u))

# Procesar eventos
for ts in "${all_timestamps[@]}"; do
    events="${events_at_time[$ts]}"
    IFS='|' read -ra event_list <<< "$events"
    
    for event in "${event_list[@]}"; do
        [[ -z "$event" ]] && continue
        IFS=':' read -ra parts <<< "$event"
        event_type="${parts[0]}"
        q="${parts[1]}"

        case "$event_type" in
            ARRIVE) query_state["$q"]="arrived" ;;
            ASSIGN)
                w="${parts[2]}"
                query_state["$q"]="running"
                query_worker["$q"]="$w"
                ;;
            IO)
                [[ "${query_state[$q]}" == "running" ]] && query_state["$q"]="io"
                ;;
            EVICT)
                # El desalojo devuelve la query al estado arrived (lista para ser reasignada)
                if [[ "${query_state[$q]}" == "running" || "${query_state[$q]}" == "io" ]]; then
                    query_state["$q"]="arrived"
                fi
                ;;
            ERROR) 
                # Solo cambiar si no está ya terminada
                if [[ "${query_state[$q]}" != "done" && "${query_state[$q]}" != "error" ]]; then
                    query_state["$q"]="error"
                    finish_order+=("$q")
                fi
                ;;
            FINISH) 
                # Solo cambiar si no está ya terminada
                if [[ "${query_state[$q]}" != "done" && "${query_state[$q]}" != "error" ]]; then
                    query_state["$q"]="done"
                    finish_order+=("$q")
                fi
                ;;
        esac
    done

    # Calcular PENDIENTES (después de procesar eventos)
    pending_list=()
    for q in "${queries[@]}"; do
        state=${query_state["$q"]}
        # PENDIENTES: queries en ejecución (running o io)
        if [[ "$state" == "running" || "$state" == "io" ]]; then
            pending_list+=("$q")
        fi
    done

    pending_str=$([[ ${#pending_list[@]} -eq 0 ]] && echo "--" || (IFS=,; echo "${pending_list[*]}"))

    # Imprimir fila
    printf "%-18s %-10s |" "$ts" "$pending_str"
    for q in "${queries[@]}"; do
        state=${query_state["$q"]:-}
        
        # Verificar qué eventos tuvo esta query en este timestamp
        has_evict=false
        has_finish=false
        IFS='|' read -ra event_list <<< "$events"
        for event in "${event_list[@]}"; do
            [[ -z "$event" ]] && continue
            IFS=':' read -ra parts <<< "$event"
            event_type="${parts[0]}"
            event_q="${parts[1]}"
            if [[ "$event_q" == "$q" ]]; then
                [[ "$event_type" == "EVICT" ]] && has_evict=true
                [[ "$event_type" == "FINISH" || "$event_type" == "ERROR" ]] && has_finish=true
            fi
        done
        
        # Prioridad: FINISH/ERROR > EVICT > estado actual
        if $has_finish; then
            if [[ "$state" == "done" ]]; then
                printf " \033[32m$FIN\033[0m"
            elif [[ "$state" == "error" ]]; then
                printf " \033[31m$ERR\033[0m"
            fi
        elif $has_evict; then
            # Mostrar EVICT aunque el estado actual sea otro (porque se reasignó en el mismo timestamp)
            printf " \033[34m$EVICT\033[0m"
        else
            # Mostrar estado actual
            case "$state" in
                arrived) printf " \033[32m·\033[0m" ;;
                running)
                    w=${query_worker["$q"]}
                    color=${WORKER_COLORS[$w]:-$NC}
                    printf " ${color}$RUN${NC}"
                    ;;
                io)
                    w=${query_worker["$q"]}
                    color=${WORKER_COLORS[$w]:-$NC}
                    printf " ${color}$IO${NC}"
                    ;;
                *) printf " ." ;;
            esac
        fi
    done
    
    # Mostrar eventos
    printf " | "
    event_comments=()
    IFS='|' read -ra event_list <<< "$events"
    for event in "${event_list[@]}"; do
        [[ -z "$event" ]] && continue
        IFS=':' read -ra parts <<< "$event"
        event_type="${parts[0]}"
        q="${parts[1]}"
        
        # Obtener el mensaje literal del log
        msg_key="$ts:$event_type:$q"
        [[ "$event_type" == "ASSIGN" ]] && msg_key="$ts:$event_type:$q:${parts[2]}"
        
        msg="${event_messages[$msg_key]}"
        if [[ -z "$msg" ]]; then
            # Si no se encuentra el mensaje, usar el formato corto
            case "$event_type" in
                ARRIVE) event_comments+=("Q$q ARRIVE") ;;
                ASSIGN) 
                    w="${parts[2]}"
                    event_comments+=("Q$q→W$w") ;;
                IO) event_comments+=("Q$q I/O") ;;
                EVICT) event_comments+=("Q$q EVICT") ;;
                ERROR) event_comments+=("Q$q ERROR") ;;
                FINISH) event_comments+=("Q$q FINISH") ;;
            esac
        else
            event_comments+=("$msg")
        fi
    done
    
    if [[ ${#event_comments[@]} -eq 0 ]]; then
        echo ""
    else
        printf "%s | " "${event_comments[0]}"
        for ((i=1; i<${#event_comments[@]}; i++)); do
            printf "%s | " "${event_comments[$i]}"
        done
        echo ""
    fi
done

echo
echo -e "\033[36mLeyenda:\033[0m"
echo -e "\033[32m·\033[0m Conectada"
for w in 1 2; do
    color=${WORKER_COLORS[$w]:-$NC}
    echo -e "${color}█${NC} CPU Worker $w  ${color}▒${NC} I/O Worker $w"
done
echo -e "\033[32m✓\033[0m Fin  \033[31m✗\033[0m Error  \033[34m↻\033[0m Desalojo"

echo
echo -e "\033[36mOrden de Finalización:\033[0m"
for ((i=0; i<${#finish_order[@]}; i++)); do
    q=${finish_order[$i]}
    state=${query_state["$q"]}
    pos=$((i+1))
    if [[ "$state" == "done" ]]; then
        echo -e "$pos. Query $q \033[32m✓\033[0m"
    elif [[ "$state" == "error" ]]; then
        echo -e "$pos. Query $q \033[31m✗\033[0m"
    fi
done