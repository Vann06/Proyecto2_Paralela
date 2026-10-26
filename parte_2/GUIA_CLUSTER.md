# Guía completa: clúster MPI con Docker (desde 0)

Guía paso a paso para levantar el clúster de `bruteforce_own` (prueba de
paralelismo con OpenMPI) en varias computadoras conectadas al mismo router.
Ejemplo con 3 máquinas (1 **HOST/master** + 2 **trabajadores**); ajusta el
número de nodos según tu caso.

## Checklist de orden (resumen)

| # | Fase | ¿Quién? |
|---|------|---------|
| 0 | Prerrequisitos | Todas |
| 1 | Clonar repo + construir imagen + levantar contenedor + SSH | Todas |
| 2 | Generar llave SSH | Solo HOST |
| 3 | Copiar la llave pública del HOST a `authorized_keys` | HOST (a sí mismo) y cada trabajador |
| 4 | Probar SSH del HOST hacia cada trabajador | Solo HOST |
| 5 | Editar `hostfile` con las IPs reales | Solo HOST |
| 6 | Preparar el log (`touch` + `tail -f`) | Todas (antes de ejecutar) |
| 7 | Lanzar `mpirun` | Solo HOST |

> Importante: los contenedores **trabajadores** deben estar corriendo con
> `sshd` ya iniciado (fin de la Fase 1) **antes** de que el HOST intente la
> Fase 4 o la Fase 7. Si un trabajador no está arriba, el HOST no puede
> conectarse a él.

---

## FASE 0 — Prerrequisitos (en CADA computadora)

- **Docker** instalado y corriendo (`docker --version`).
- **Git** instalado (`git --version`).
- Todas las máquinas conectadas al **mismo router / misma red local**.
- Firewall: permitir el puerto **2222/tcp** (SSH del contenedor) entre las
  máquinas del clúster. Si usas `ufw`:
  ```bash
  sudo ufw allow 2222/tcp
  ```
  En Windows (si usas WSL2, ver nota al final), permite Docker/WSL en el
  Firewall de Windows cuando lo pida.
- Anota la **IP de cada máquina** en la red del router:
  ```bash
  ip a
  ```
  (buscar la IP tipo `192.168.1.x` de la interfaz conectada al router).
  Decide cuál máquina será el **HOST (master)**.

---

## FASE 1 — En CADA computadora (HOST y trabajadores)

### 1.1 Clonar el repositorio
```bash
git clone <url-del-repo>
cd Proyecto2_Paralela
```
> Si ya lo tenías clonado, solo asegúrate de estar en la rama/commit correcto
> (`git pull`). Si hiciste `git pull` con cambios de código, ver la nota al
> final sobre reconstruir la imagen.

### 1.2 Construir la imagen
Desde la **raíz** del repositorio:
```bash
docker build -t mpi-cluster-image -f parte_2/Dockerfile .
```

### 1.3 Levantar el contenedor y arrancar SSH
```bash
docker run -it --network host --name mpi_node mpi-cluster-image /bin/bash
service ssh start
```
> Déjalo abierto (no cierres esta terminal). El `sshd` del contenedor
> escucha en el puerto **2222**.
> Si sale *"name already in use"*, borra el viejo con `docker rm -f mpi_node`
> y repite este paso.

En este punto **todas** las máquinas (HOST y trabajadores) deben tener su
contenedor corriendo con `service ssh start` ya ejecutado.

---

## FASE 2 — Generar la llave SSH (SOLO en el HOST)

Dentro del contenedor del **HOST**:
```bash
ssh-keygen -t rsa -b 4096 -N "" -f /root/.ssh/id_rsa
```
> `-N ""` = sin passphrase (necesario para que MPI entre sin pedir contraseña).

Muestra la llave **pública** y cópiala (la vas a necesitar en todos lados):
```bash
cat /root/.ssh/id_rsa.pub
```

---

## FASE 3 — Repartir la llave pública del HOST a TODOS

La llave **pública del HOST** debe quedar en `~/.ssh/authorized_keys` de
**todos** los contenedores: los trabajadores **y el propio HOST** (MPI
también se conecta a sí mismo al arrancar procesos locales).

### 3.1 En el HOST (a sí mismo)
```bash
cat /root/.ssh/id_rsa.pub >> /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys
```

### 3.2 En CADA TRABAJADOR
Dentro del contenedor de cada trabajador, pega la línea que copiaste del HOST:
```bash
mkdir -p /root/.ssh && chmod 700 /root/.ssh
echo "ssh-rsa AAAA....la-llave-publica-del-host... root@..." >> /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys
```
> Reemplaza el `echo "..."` por el contenido exacto de `id_rsa.pub` del HOST.
> No hace falta generar una llave en los trabajadores: solo el HOST inicia
> conexiones SSH (el HOST nunca necesita la llave de un trabajador).

---

## FASE 4 — Probar la conexión SSH (desde el HOST)

Dentro del contenedor del HOST, prueba entrar a cada trabajador:
```bash
ssh 192.168.1.11 hostname
ssh 192.168.1.12 hostname
```
Debe responder el hostname **sin pedir contraseña**. Si pide contraseña,
la llave no quedó bien en ese trabajador (repite FASE 3.2) o el trabajador
no tiene `sshd` corriendo (repite FASE 1.3 en ese trabajador).

---

## FASE 5 — Configurar el hostfile (SOLO en el HOST)

Dentro del contenedor del HOST, edita el hostfile con las IPs **reales**,
el **HOST primero**:
```bash
nano hostfile
```
Ejemplo (estás en `/app`):
```
192.168.1.10 slots=1
192.168.1.11 slots=1
192.168.1.12 slots=1
```

Prueba que MPI vea todos los nodos:
```bash
mpirun --hostfile hostfile -np 3 hostname
```
Debe imprimir los 3 hostnames (sin colgarse ni pedir contraseña).

---

## FASE 6 — Preparar el log (en CADA máquina: HOST y trabajadores)

Antes de ejecutar el reto, en cada contenedor abre otra terminal y deja el
log corriendo:
```bash
touch /tmp/bruteforce.log
tail -f /tmp/bruteforce.log
```
> Esto es lo que te deja ver el progreso de **esa** máquina en tiempo real
> (el HOST solo muestra en pantalla la salida final combinada, no el avance
> de cada trabajador).

---

## FASE 7 — Ejecutar el reto (SOLO desde el HOST)

```bash
mpirun -x BRUTEFORCE_LOG=/tmp/bruteforce.log \
       --hostfile hostfile -np 3 ./bruteforce_own 999999 1000000 texto.txt
```

> **Si `mpirun` se cuelga**, limita MPI a la subred del router (evita
> interfaces de Docker/VPN):
> ```bash
> mpirun --mca btl_tcp_if_include 192.168.1.0/24 --mca oob_tcp_if_include 192.168.1.0/24 \
>        -x BRUTEFORCE_LOG=/tmp/bruteforce.log \
>        --hostfile hostfile -np 3 ./bruteforce_own 999999 1000000 texto.txt
> ```
> Ajusta `192.168.1.0/24` a la subred real de tu router.

La salida combinada de todos los procesos aparece **solo en la terminal
del HOST**. El progreso de cada máquina individual se ve en su propio
`tail -f /tmp/bruteforce.log` (FASE 6).

---

## Notas rápidas

- **Windows:** la red `host` de Docker Desktop **no sirve** para MPI (el
  contenedor solo ve IPs internas `192.168.65.x`). Usa Docker Engine dentro
  de WSL2 con `networkingMode=mirrored` en `%USERPROFILE%\.wslconfig`.
- **Si reinicias el contenedor** (no lo borras): la llave SSH se conserva,
  pero hay que repetir `service ssh start` (FASE 1.3) en esa máquina.
  - Reusar el contenedor existente: `docker start -ai mpi_node`
    (o `docker start mpi_node` + `docker exec -it mpi_node /bin/bash`).
- **Si hiciste `git pull` con cambios de código** (afecta `parte_1/`,
  `hostfile` o el `Dockerfile`): el contenedor viejo sigue con el código
  viejo, porque el código se copia dentro de la imagen en build time. Hay
  que reconstruir la imagen y crear un contenedor nuevo en **esa** máquina
  → se repite desde la FASE 1.2, y la llave SSH desde la FASE 3 (un
  contenedor nuevo no conserva la capa de escritura del viejo):
  ```bash
  docker build -t mpi-cluster-image -f parte_2/Dockerfile .
  docker rm -f mpi_node
  docker run -it --network host --name mpi_node mpi-cluster-image /bin/bash
  service ssh start
  ```
