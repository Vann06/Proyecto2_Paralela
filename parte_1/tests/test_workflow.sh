#!/usr/bin/env bash
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'echo "Archivos de prueba: $work"' EXIT
target='pan de banano rico y saludable'
printf 'Texto de prueba: %s.\n' "$target" > "$work/plain.txt"
cd -- "$work"
for format in hex base64 binary; do
    mpirun -np 1 "$project/bruteforce_own" encrypt --key 7 --input plain.txt \
        --target "$target" --format "$format" --output "$format.cipher" > /dev/null
    for p in 1 4; do
        mpirun -np "$p" "$project/bruteforce_own" crack --input "$format.cipher" \
            --format "$format" --target "$target" --upper 32 --output recovered.txt \
            > "$format.p$p.log"
        cmp -- plain.txt recovered.txt
        grep -q "^RESULT,7,$p," "$format.p$p.log"
    done
done
bash "$project/benchmark_recibido.sh" --input base64.cipher --format base64 \
    --target "$target" --upper 32 --processes '1 2' --repetitions 1 \
    --executable "$project/bruteforce_own" --output-dir "$work/benchmark" > /dev/null
[[ $(wc -l < "$work/benchmark/resultados.csv") == 3 ]]
[[ $(wc -l < "$work/benchmark/resumen.csv") == 3 ]]
echo 'OK: tres formatos, MPI con 1 y 4 procesos y benchmark Bash con CSV y logs.'
