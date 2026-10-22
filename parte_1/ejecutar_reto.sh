#!/usr/bin/env bash
# Run our received challenge and keep the complete log beside the plaintext.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

processes=${1:-4}
output=${2:-"ejecuciones/reto_$(date +%Y%m%d_%H%M%S)_p${processes}"}
if [[ $# -gt 2 || ! $processes =~ ^[1-9][0-9]*$ ]]; then
    echo "Uso: bash ejecutar_reto.sh [procesos=4] [carpeta_nueva]" >&2
    exit 1
fi
make
mkdir -p -- "$(dirname -- "$output")"
mkdir -- "$output"
mpirun -np "$processes" ./bruteforce_own crack \
    --input recibido_base64.txt --format base64 \
    --target "computación paralela y distribuida" --upper 4000000 \
    --output "$output/texto_recuperado.txt" 2>&1 | tee "$output/ejecucion.log"
echo "Log: $output/ejecucion.log"
echo "Texto: $output/texto_recuperado.txt"
