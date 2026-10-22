#!/bin/bash

# ============================================================
# CONFIGURACIÓN
# ============================================================

PROCESOS=4
UPPER=1000000
REPETICIONES=5
ARCHIVO="texto.txt"

# Llaves que vamos a probar
KEYS=(
    5000
    255000
    625000
    995000
)

OUTPUT="resultados.csv"


# ============================================================
# CABECERA CSV
# ============================================================

echo "key,procesos,repeticion,tiempo,intentos" > "$OUTPUT"


# ============================================================
# PRUEBAS
# ============================================================

for KEY in "${KEYS[@]}"
do

    echo
    echo "=========================================="
    echo "Probando llave: $KEY"
    echo "=========================================="

    for RUN in $(seq 1 $REPETICIONES)
    do

        echo
        echo "Ejecucion $RUN de $REPETICIONES"


        RESULT=$(
            mpirun \
            -np "$PROCESOS" \
            ./bruteforce_own \
            "$KEY" \
            "$UPPER" \
            "$ARCHIVO" \
            | grep "^RESULT,"
        )


        IFS=',' read \
            LABEL \
            KEY_RESULT \
            PROCESS_RESULT \
            TIME_RESULT \
            ATTEMPTS_RESULT \
            <<< "$RESULT"


        echo \
"$KEY_RESULT,$PROCESS_RESULT,$RUN,$TIME_RESULT,$ATTEMPTS_RESULT" \
        >> "$OUTPUT"


        echo "Tiempo: $TIME_RESULT s"

    done

done


# ============================================================
# MOSTRAR PROMEDIOS
# ============================================================

echo
echo "=========================================="
echo "PROMEDIOS"
echo "=========================================="


for KEY in "${KEYS[@]}"
do

    awk \
    -F',' \
    -v key="$KEY" \
    '
    NR > 1 && $1 == key
    {
        sum += $4
        count++
    }

    END
    {
        if(count > 0)
        {
            printf "Llave %-10s -> %.9f segundos\n",
                   key,
                   sum / count
        }
    }
    ' \
    "$OUTPUT"

done


echo
echo "=========================================="
echo "PRUEBAS COMPLETADAS"
echo "=========================================="

echo
echo "Resultados guardados en:"
echo "$OUTPUT"
