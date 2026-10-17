# Proyecto 2 — Búsqueda de llave DES por Fuerza Bruta con MPI

Proyecto de **Computación Paralela y Distribuida** para realizar la búsqueda por fuerza bruta de una llave DES utilizando MPI.

## Archivos

- `bruteforce_own.c`: programa principal de cifrado, descifrado y búsqueda de la llave con MPI.
- `benchmark.sh`: ejecuta varias pruebas con distintas llaves y registra los tiempos.
- `texto.txt`: texto utilizado para las pruebas de cifrado.
- `README.md`: instrucciones del proyecto.

## Requisitos

- Linux o WSL
- GCC
- OpenMPI
- OpenSSL
- Bash

En Ubuntu/WSL:

```bash
sudo apt update
sudo apt install build-essential openmpi-bin libopenmpi-dev libssl-dev
```

## Compilación

Desde la carpeta del proyecto:

```bash
mpicc -O2 bruteforce_own.c -o bruteforce_own -lcrypto
```

## Ejecución

Formato:

```bash
mpirun -np <procesos> ./bruteforce_own <llave> <limite> <archivo>
```

Ejemplo:

```bash
mpirun -np 4 ./bruteforce_own 625000 1000000 texto.txt
```

Donde:

- `4` es la cantidad de procesos MPI.
- `625000` es la llave utilizada para cifrar el texto.
- `1000000` es el límite superior del espacio de búsqueda.
- `texto.txt` es el archivo utilizado para la prueba.

## Benchmark

El script `benchmark.sh` ejecuta varias mediciones automáticamente utilizando diferentes llaves.

Primero dar permisos de ejecución:

```bash
chmod +x benchmark.sh
```

Luego ejecutar:

```bash
./benchmark.sh
```

El benchmark realiza varias repeticiones por llave y genera un archivo `resultados.csv` con:

- llave probada;
- cantidad de procesos;
- número de repetición;
- tiempo de búsqueda;
- cantidad de intentos.

Antes de ejecutar el benchmark, el programa debe estar compilado como:

```text
bruteforce_own
```

## Tecnologías utilizadas

- C
- MPI / OpenMPI
- OpenSSL
- DES-ECB
- Bash
- Linux / WSL
- Git
