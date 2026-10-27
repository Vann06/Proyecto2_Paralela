#!/bin/bash
# Envoltorio de mpirun que detecta automaticamente la subred real de la
# maquina (la de la interfaz con ruta por defecto, ej. wlp2s0, eth0) y
# limita MPI a esa subred con --mca btl_tcp_if_include / oob_tcp_if_include.
#
# Esto evita que mpirun intente usar las interfaces internas de Docker
# (docker0, br-xxxxxxxx, veth...) que aparecen en los contenedores y que
# cuelgan la conexion entre nodos remotos (ver seccion 5 del README).
#
# Uso: igual que mpirun, solo cambia el nombre del comando.
#   ./mpirun_lan.sh --hostfile hostfile -np 3 ./bruteforce_own 999999 1000000 texto.txt

set -euo pipefail

IFACE=$(ip route show default | awk '/default/ {print $5; exit}')
if [ -z "${IFACE:-}" ]; then
    echo "[mpirun_lan] No se pudo detectar la interfaz de red por defecto (ip route show default)." >&2
    exit 1
fi

SUBNET=$(ip -4 -o addr show "$IFACE" | awk '{print $4; exit}')
if [ -z "${SUBNET:-}" ]; then
    echo "[mpirun_lan] No se pudo detectar la IP/subred de la interfaz '$IFACE'." >&2
    exit 1
fi

echo "[mpirun_lan] interfaz=$IFACE subred=$SUBNET (excluyendo interfaces de Docker)" >&2

exec mpirun --mca btl_tcp_if_include "$SUBNET" --mca oob_tcp_if_include "$SUBNET" "$@"
