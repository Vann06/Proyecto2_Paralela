#!/usr/bin/env bash
# Bash + awk only: same challenge and bound for every MPI run.
set -euo pipefail

usage() {
    echo 'Uso: bash benchmark_recibido.sh --input ARCHIVO --format hex|base64|binary'
    echo '  --target "frase exacta" --upper N [--processes "1 2 4"]'
    echo '  [--repetitions 3] [--padding zero|pkcs7|none] [--output-dir CARPETA_NUEVA]'
    echo '  [--executable ./bruteforce_own] [--hostfile hosts.txt]'
}
input='' format='' target='' upper='' padding=zero repetitions=3
processes='1 2 4' executable=./bruteforce_own hostfile=''
output="ejecuciones/benchmark_$(date +%Y%m%d_%H%M%S)"
while (($#)); do
    if [[ $1 == --help ]]; then usage; exit 0; fi
    if (($# < 2)); then usage >&2; exit 1; fi
    case $1 in
        --input) input=$2 ;; --format) format=$2 ;; --target) target=$2 ;;
        --upper) upper=$2 ;; --padding) padding=$2 ;; --repetitions) repetitions=$2 ;;
        --processes) processes=$2 ;; --executable) executable=$2 ;;
        --hostfile) hostfile=$2 ;; --output-dir) output=$2 ;;
        *) usage >&2; exit 1 ;;
    esac
    shift 2
done
if [[ ! -f $input || -z $target || ! $upper =~ ^[1-9][0-9]*$ ||
      ! $repetitions =~ ^[1-9][0-9]*$ ]]; then usage >&2; exit 1; fi
case $format in hex|base64|binary) ;; *) usage >&2; exit 1 ;; esac
case $padding in zero|pkcs7|none) ;; *) usage >&2; exit 1 ;; esac
if [[ ! -x $executable ]]; then
    echo "Falta el ejecutable. Compila primero con make." >&2; exit 1
fi
read -r -a requested <<< "$processes"
counts=(1)
for p in "${requested[@]}"; do
    if [[ ! $p =~ ^[1-9][0-9]*$ ]]; then usage >&2; exit 1; fi
    seen=0
    for existing in "${counts[@]}"; do [[ $existing != "$p" ]] || seen=1; done
    if ((seen == 0)); then counts+=("$p"); fi
done
mkdir -p -- "$(dirname -- "$output")"
mkdir -- "$output"
printf 'procesos,repeticion,llave,tiempo_s,intentos,ts_medio_s,speedup,eficiencia_pct\n' > "$output/resultados.csv"
printf 'procesos,tiempo_medio_s,speedup,eficiencia_pct\n' > "$output/resumen.csv"
{
    printf 'Archivo: %s\nFormato: %s\nObjetivo: %s\nLimite: %s\nPadding: %s\n' \
        "$input" "$format" "$target" "$upper" "$padding"
    printf 'Procesos: %s\nRepeticiones: %s\nEjecutable: %s\nHostfile: %s\n' \
        "${counts[*]}" "$repetitions" "$executable" "$hostfile"
} > "$output/parametros.txt"
baseline='' reference='' found_key=''
for p in "${counts[@]}"; do
    samples="$output/p${p}_muestras.csv"
    : > "$samples"
    for ((run=1; run<=repetitions; ++run)); do
        stem="$output/p${p}_r${run}"
        command=(mpirun -np "$p")
        if [[ -n $hostfile && $p != 1 ]]; then command+=(--hostfile "$hostfile"); fi
        command+=("$executable" crack --input "$input" --format "$format"
            --target "$target" --upper "$upper" --padding "$padding"
            --output "${stem}_recuperado.txt")
        echo "Procesos: $p; repeticion: $run"
        if ! "${command[@]}" 2>&1 | tee "${stem}.log"; then
            echo "Prueba fallida: ${stem}.log. No se calcula speedup." >&2; exit 1
        fi
        result=$(awk -F, '/^RESULT,[0-9]+,[0-9]+,[0-9.]+,[0-9]+$/ { print; n++ }
            END { if(n != 1) exit 1 }' "${stem}.log")
        IFS=, read -r label key actual seconds attempts <<< "$result"
        if [[ $actual != "$p" ]] || ! awk -v t="$seconds" 'BEGIN { exit !(t > 0) }'; then
            echo "Resultado o tiempo invalido en ${stem}.log" >&2; exit 1
        fi
        if [[ -z $reference ]]; then reference="${stem}_recuperado.txt"; found_key=$key; fi
        if [[ $key != "$found_key" ]] || ! cmp -s -- "$reference" "${stem}_recuperado.txt" ||
           ! grep -Fq -- "$target" "${stem}_recuperado.txt"; then
            echo "La llave o el texto recuperado no coinciden entre pruebas." >&2; exit 1
        fi
        printf '%s,%s,%s,%s,%s\n' "$p" "$run" "$key" "$seconds" "$attempts" >> "$samples"
    done
    mean=$(awk -F, '{sum += $4} END {printf "%.9f", sum/NR}' "$samples")
    if [[ $p == 1 ]]; then baseline=$mean; fi
    awk -F, -v ts="$baseline" 'BEGIN {OFS=","}
        {printf "%s,%.9f,%.6f,%.4f\n", $0, ts, ts/$4, 100*ts/($1*$4)}' \
        "$samples" >> "$output/resultados.csv"
    awk -v p="$p" -v mean="$mean" -v ts="$baseline" \
        'BEGIN {printf "%d,%.9f,%.6f,%.4f\n", p,mean,ts/mean,100*ts/(p*mean)}' \
        >> "$output/resumen.csv"
done
echo "Resultados por ejecucion: $output/resultados.csv"
echo "Resumen sobre medias: $output/resumen.csv"
