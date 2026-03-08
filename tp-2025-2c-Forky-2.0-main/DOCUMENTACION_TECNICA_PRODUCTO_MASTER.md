# Documentación Técnico-Producto del Sistema
## Proyecto clave de Ingeniería en Sistemas de Información (UTN)

**Repositorio:** tp-2025-2c-Forky-2.0-main  
**Módulo foco:** `MASTER`  
**Fecha:** 2026-03-08  
**Perfil del documento:** técnico-producto (académico + ingeniería)

---

## 1. Síntesis técnico-producto del sistema (visión global)

Este proyecto implementa una **plataforma distribuida de ejecución de queries** orientada a prácticas avanzadas de Sistemas Operativos, con separación explícita de responsabilidades por procesos:

- **Query Control**: interfaz cliente que envía una query (ruta + prioridad), recibe confirmación, mensajes de lectura intermedios y resultado final.
- **Master**: orquestador central; administra sesiones de query, registro de workers, asignación por algoritmo de planificación y consolidación de resultados.
- **Worker**: ejecutor de instrucciones; interpreta queries, interactúa con Storage y reporta progreso/resultado/error al Master.
- **Storage**: backend de persistencia sobre bloques/metadata para operaciones de archivos y tags.
- **Utils**: biblioteca común de red/protocolo/log/config para homogeneidad entre módulos.

Desde perspectiva de producto, el sistema resuelve:

1. **Admisión de carga** (submit de queries concurrentes).
2. **Planificación multiproceso** (FIFO o prioridades con aging).
3. **Ejecución distribuida** (workers desacoplados del cliente emisor).
4. **Observabilidad operativa** (logging de conexión, asignación, desalojo, finalización, error).
5. **Tolerancia parcial a fallos** (desconexiones y watchdog de consistencia de estado).

Como proyecto de carrera (UTN), integra competencias de: diseño de protocolos, concurrencia, sincronización, administración de recursos, planificación, manejo de fallas y validación end-to-end.

---

## 2. Arquitectura y modelo de interacción

## 2.1 Arquitectura lógica

- **Cliente externo**: Query Control.
- **Plano de control**: Master.
- **Plano de ejecución**: Workers.
- **Plano de datos**: Storage.

La topología principal es **Master-céntrica** para coordinación:

- Query Control ↔ Master
- Worker ↔ Master
- Worker ↔ Storage

No existe acoplamiento directo Query Control ↔ Worker ni Query Control ↔ Storage.

## 2.2 Flujo end-to-end resumido

1. Query Control envía `OP_QUERY_SUBMIT` con `query_path` y `priority`.
2. Master asigna `query_id`, crea sesión, responde `OP_QUERY_CONFIRM`.
3. Scheduler selecciona query y worker según algoritmo.
4. Master envía asignación al Worker (`OP_MASTER_SEND_PATH`).
5. Worker ejecuta instrucciones; si hay `READ`, envía `OP_WORKER_READ_RESULT`.
6. Master reenvía lectura al Query Control (`OP_QUERY_READ_MESSAGE`).
7. Finalización:
   - Éxito: `OP_WORKER_QUERY_FINISHED` → Master responde `SUCCESS` al Query Control.
   - Error funcional: `OP_WORKER_QUERY_ERROR` → Master propaga detalle de error.
   - Resultado completo alternativo: `OP_MASTER_QUERY_COMPLETE`.

---

## 3. Protocolo y contratos de comunicación (ES/EN)

El protocolo es binario por opcodes, con framing y serialización en `utils`.

## 3.1 Master ↔ Query Control

- `OP_QUERY_SUBMIT`: submit de query con prioridad.
- `OP_QUERY_CONFIRM`: confirmación con `query_id` asignado.
- `OP_QUERY_READ_MESSAGE`: mensaje intermedio de lectura (`file:tag|contenido`).
- `OP_QUERY_RESULT`: resultado final (`SUCCESS`, `ERROR`, o payload textual).

## 3.2 Master ↔ Worker

- `OP_MASTER_WORKER_REGISTER` / `ACK`: alta de worker.
- `OP_MASTER_SEND_PATH`: dispatch de query + path + program counter (`pc`).
- `OP_MASTER_EVICT` / `OP_WORKER_EVICT_ACK`: desalojo (preemption).
- `OP_WORKER_READ_RESULT`: avance de lectura.
- `OP_WORKER_QUERY_FINISHED`: finalización exitosa.
- `OP_WORKER_QUERY_ERROR`: finalización con error.
- `OP_MASTER_QUERY_COMPLETE`: vía de resultado completo.

## 3.3 Worker ↔ Storage

Operaciones de filesystem lógico (`CREATE`, `READ`, `WRITE`, `TRUNCATE`, `TAG`, `DELETE`, `FLUSH`, `COMMIT`) con códigos `ST_OK`/`ST_ERROR`.

---

## 4. Módulo MASTER: definición técnico-producto

## 4.1 Rol de negocio y de ingeniería

El `MASTER` es el **scheduler-dispatcher central** del sistema. Su contrato funcional es:

- aceptar pedidos,
- administrar cola/sesiones,
- asignar ejecución con política configurable,
- supervisar consistencia del estado distribuido,
- devolver respuesta final al cliente.

En términos de ingeniería, combina **server concurrente + planificador + monitor de salud**.

## 4.2 Estructuras de datos núcleo

### a) `t_query_session`
Representa una query viva en el sistema:

- identidad: `query_id`
- sockets asociados: Query Control / Worker
- ejecución: `query_path`, `pc`, `is_active`, `estado`
- scheduling: `priority`, `original_priority`, `ready_since`
- control de preemption: `version` para validar `EVICT_ACK`

### b) `t_worker_info`
Representa un worker registrado:

- identidad: `worker_id`
- conexión: `socket_conn`
- ocupación: `is_busy`
- control de desalojo: `awaiting_evict_response`, `evict_request_time`

### c) Estructuras compartidas

- `query_sessions` (`t_list`) + `queries_by_id` (`t_dictionary`) para búsqueda O(1).
- `workers_list` (`t_list`) + `workers_by_socket` (`t_dictionary`) para lookup rápido.
- `query_next_id` con mutex para asignación atómica.
- `worker_count` con mutex para métrica de multiprocesamiento.

## 4.3 Ciclo de vida de MASTER

1. Validación de parámetros y carga de configuración.
2. Inicialización de logger y validación de algoritmo (`FIFO`/`PRIORIDADES`).
3. Creación del server socket.
4. Inicialización de listas, diccionarios y primitivas de sincronización.
5. Lanzamiento de hilos de fondo:
   - monitor de conexiones,
   - scheduler,
   - aging (si corresponde),
   - watchdog.
6. Loop principal de `accept` + handler por cliente.

## 4.4 Lógica de admisión y clasificación de clientes

El handler identifica el tipo de cliente por `opcode` inicial (`MSG_PEEK`):

- Query Control (`OP_QUERY_SUBMIT`): crea sesión y confirma id.
- Worker (`OP_MASTER_WORKER_REGISTER`): registra worker, incrementa contador y lo deja `idle`.

Esto permite conexiones persistentes con flujo de mensajes asincrónico posterior.

---

## 5. Planificación y asignación (Scheduling/Dispatch)

## 5.1 Algoritmos soportados

### FIFO
- Selección de la primera query `READY`.
- Asignación al primer worker no ocupado.
- Sin desalojo por prioridad.

### PRIORIDADES
- Ordenación por prioridad ascendente (menor número = mayor prioridad).
- Si hay worker libre: asignación directa.
- Si no hay worker libre: evaluación de preemption sobre query activa de menor prioridad.

## 5.2 Aging

Cuando `TIEMPO_AGING > 0`:

- las queries en `READY` reducen su valor numérico de prioridad (hasta mínimo 1),
- se reinicia `ready_since`,
- se reordena la cola,
- se señaliza al scheduler.

Objetivo producto: mitigar starvation y mejorar fairness bajo alta contención.

## 5.3 Preemption (Desalojo)

La política implementa desalojo con handshake:

1. Master decide query víctima (`RUNNING` con prioridad menor que la entrante).
2. Incrementa `version` de sesión y envía `OP_MASTER_EVICT`.
3. Worker responde `OP_WORKER_EVICT_ACK` con `pc` y `version`.
4. Master valida versión antes de aceptar ACK.
5. Query vuelve a `READY` con `pc` actualizado.

### Diseño esperado ideal

- preemption segura por versión,
- no reasignar worker hasta ACK válido o timeout controlado,
- replanificación inmediata posterior.

### Estado observado

- existe control por versión y flags de espera de ACK,
- conviven trazas de enfoques alternativos (bloques comentados),
- hay zonas de evolución pendientes para robustez total en escenarios de timeout severo.

---

## 6. Concurrencia, sincronización y threading

## 6.1 Hilos principales

- **Main accept loop**: admisión de sockets.
- **Handler por cliente**: procesamiento continuo de mensajes por conexión.
- **Scheduler thread**: FIFO o PRIORIDADES.
- **Aging thread**: envejecimiento de prioridades (opcional).
- **Connection checker**: detección de desconexiones.
- **Watchdog**: verificación de estados colgados/inconsistentes.

## 6.2 Primitivas utilizadas

- `pthread_rwlock_t` para listas/diccionarios compartidos.
- `pthread_mutex_t` para contadores y coordinación puntual.
- `pthread_cond_t` para scheduler reactivo por eventos.
- `_Atomic(time_t)` en `ready_since` para lecturas/escrituras temporales seguras.

### Interpretación orientada a semáforos

El modelo de sincronización de MASTER sigue una **lógica basada en semáforos**, aunque en este código no se declaren explícitamente objetos POSIX `sem_t`:

- La **semántica de semáforo binario** se implementa con `pthread_mutex_t` (exclusión mutua en secciones críticas como `query_next_id` y `worker_count`).
- La **semántica de semáforo de lectura/escritura** se implementa con `pthread_rwlock_t` (múltiples lectores concurrentes, un único escritor sobre estructuras compartidas del scheduler).
- La **señalización tipo semáforo de eventos/contador** se implementa con `pthread_cond_t` + `pthread_mutex_t` (despertar del scheduler ante nuevas queries, workers libres o cambios de prioridad).

Desde ingeniería de sistemas, esto preserva el objetivo conceptual de los semáforos: acceso controlado, ordenamiento determinístico de actualizaciones críticas y coordinación segura entre hilos bajo contención.

## 6.3 Patrón de sincronización

- lectura concurrente permitida en estructuras de consultas frecuentes,
- escritura exclusiva para cambios de estado,
- señalización explícita al scheduler en eventos relevantes (nueva query, worker libre, cambio por aging/desalojo).

## 6.4 Consideraciones técnicas

- Se prioriza throughput con hilos detached.
- No hay secuencia formal de shutdown ordenado (diseño de proceso de larga vida).
- Ciertas rutas requieren disciplina estricta de lock-order para evitar races y deadlocks en futuras extensiones.

---

## 7. Gestión de memoria y recursos

## 7.1 Modelo de ownership

- `t_query_session` y `t_worker_info` son objetos heap con ciclo de vida explícito.
- Se proveen funciones de liberación dedicadas:
  - `free_query_session`
  - `free_worker_info`

## 7.2 Estrategia operativa

- Alta: al registrar worker o aceptar submit.
- Mutación: durante scheduler/eventos.
- Baja: al terminar query, error, desconexión o limpieza de worker.

## 7.3 Recursos de red

- Sockets por cliente en conexión persistente.
- Cierre ante desconexión detectada y limpieza de estructuras asociadas.

## 7.4 Riesgos de memoria/recursos

- Por diseño threaded persistente, la higiene de cleanup por rama de error es crítica.
- No existe (en estado actual) una fase integral de `graceful shutdown` que compacte toda la liberación de recursos de proceso.

---

## 8. Validación, manejo de errores y resiliencia

## 8.1 Validaciones de entrada y configuración

- validación de argumentos de arranque,
- validación de algoritmo de planificación,
- manejo de errores de configuración y creación de servidor.

## 8.2 Errores de ejecución

`MASTER` propaga errores funcionales provenientes de Worker hacia Query Control, preservando trazabilidad por `query_id`.

Casos cubiertos por pruebas:

- creación de archivo existente,
- escritura sobre archivo ya committeado,
- lectura fuera de límite,
- tag duplicado.

## 8.3 Manejo de desconexiones

- **Query Control desconectado**: la query asociada se finaliza/cancela en coordinación con estado del worker.
- **Worker desconectado**: reducción de capacidad, limpieza de estado y cierre de sesiones impactadas.

## 8.4 Watchdog

Funciones principales:

1. detectar queries activas con tiempo excesivo,
2. corregir workers marcados ocupados sin query válida,
3. gestionar timeout de espera de `EVICT_ACK`.

### Diseño esperado ideal

- autocuración de inconsistencias sin bloquear scheduler,
- recuperación gradual de capacidad ante workers problemáticos,
- alarmado temprano por tiempos anómalos.

### Estado observado

- watchdog operativo y útil como defensa de consistencia,
- recuperación total automática de query bloqueada aún no completamente cerrada en todas las rutas.

---

## 9. Seguridad y superficie de ataque

## 9.1 Evaluación técnica

El sistema tiene foco académico/funcional, con seguridad de transporte y autenticación **no endurecida**:

- sin TLS,
- sin autenticación fuerte de cliente,
- sin autorización por rol más allá del protocolo por opcode.

## 9.2 Riesgos principales

- spoofing de cliente que hable el protocolo,
- inyección de payloads largos o malformados,
- uso de rutas de query sin normalización estricta de policy de path.

## 9.3 Endurecimientos recomendados (roadmap)

1. mTLS o canal cifrado.
2. handshake autenticado por token/certificado.
3. límites globales de payload y rate limiting por socket.
4. validación fuerte de `query_path` (allowlist + canonicalización).
5. auditoría de eventos de seguridad separada del log funcional.

---

## 10. Configuración operativa y despliegue

## 10.1 Parámetros de MASTER

- `PUERTO_ESCUCHA`
- `IP_ESCUCHA`
- `LOG_LEVEL`
- `ALGORITMO_PLANIFICACION`
- `TIEMPO_AGING`

## 10.2 Build y dependencia

- compilación por módulo con `make`,
- dependencia común `so-commons-library`,
- scripts de compilación orquestada (`compilar_todo.sh`),
- soporte de despliegue con Docker y scripts de ejecución.

## 10.3 Operación esperada

- iniciar Storage,
- iniciar Master,
- registrar Workers,
- ejecutar Query Controls,
- monitorear logs y resultados de test scripts.

---

## 11. Validación del producto (tests y calidad)

## 11.1 Evidencia funcional relevante

- suites de FIFO y PRIORIDADES,
- stress de aging con múltiples queries concurrentes,
- suite de propagación de errores end-to-end,
- scripts de estabilidad general.

## 11.2 Criterios de aceptación técnico-producto

1. admisión correcta de submit con confirmación de id.
2. asignación consistente según algoritmo configurado.
3. propagación de mensajes READ intermedios al cliente.
4. resultado final correcto (`SUCCESS`/`ERROR`/payload).
5. reacción consistente ante desconexiones de cliente o worker.
6. fairness observable en PRIORIDADES + aging.

---

## 12. Estado objetivo (ideal) vs estado implementado

| Área | Estado objetivo ideal | Estado observado |
|---|---|---|
| Scheduling FIFO | determinista, sin preemption | implementado |
| Scheduling prioridades | selección por prioridad + preemption estable | implementado con evolución en ramas de timeout/ack |
| Aging | anti-starvation configurable | implementado cuando `TIEMPO_AGING > 0` |
| Resiliencia | autocuración de inconsistencias | watchdog operativo, recuperación total aún incremental |
| Propagación de errores | E2E y trazable por query_id | implementada y testeada en scripts dedicados |
| Seguridad | autenticación + cifrado + hardening | baseline funcional sin hardening fuerte |

---

## 13. Riesgos técnicos y deuda priorizada

### Riesgos de mayor impacto

1. condiciones de carrera en escenarios de alta competencia entre scheduler, handlers y monitoreo de conexiones.
2. ausencia de shutdown ordenado para limpieza integral de recursos.
3. seguridad de red mínima (entorno académico, no productivo internet-exposed).

### Deuda técnica priorizada

1. unificar definitivamente estrategia de preemption/ACK/timeouts en un solo flujo.
2. formalizar cancelación explícita de query por desconexión de Query Control.
3. externalizar parámetros de watchdog (interval/timeout) a configuración.
4. ampliar test de concurrencia extrema y fallas de red intermitentes.

---

## 14. Conclusión académica

El sistema representa un **caso integrador sólido** para Ingeniería en Sistemas de Información (UTN), combinando diseño distribuido, planificación de procesos, sincronización multihilo y control operativo. Dentro del ecosistema, `MASTER` cumple un rol central y de alta complejidad: sostiene el modelo de ejecución, aplica políticas de fairness, coordina la tolerancia parcial a fallos y materializa el contrato de servicio hacia el usuario.

En síntesis técnico-producto, el proyecto demuestra un nivel avanzado para contexto de carrera: arquitectura modular clara, protocolo propio operativo y capacidades de scheduling concurrente reales, con una hoja de evolución bien identificada hacia mayor robustez y seguridad.

---

## 15. Glosario breve (ES/EN)

- **Query Session**: contexto de vida de una query.
- **Scheduler**: componente de planificación de asignaciones.
- **Dispatch**: envío efectivo de query a worker.
- **Preemption / Evict**: desalojo de query en ejecución.
- **Aging**: mejora progresiva de prioridad para evitar starvation.
- **Watchdog**: hilo de vigilancia de salud de estado.
- **Ready / Running**: estados de una query esperando o ejecutando.
- **Multiprocesamiento (nivel)**: cantidad de workers activos disponibles para ejecución concurrente.
