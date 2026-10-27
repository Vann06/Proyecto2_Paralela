# Guía completa: clúster MPI con Docker (desde 0)

Guía paso a paso para levantar el clúster de `bruteforce_own` (prueba de
paralelismo con OpenMPI) en varias computadoras conectadas al mismo router.
Ejemplo con 3 máquinas (1 **HOST/master** + 2 **trabajadores**); ajusta el
número de nodos según tu caso.

## Checklist de orden (resumen)

| # | Fase | ¿Quién? |
|---|------|---------|
| 0 | Prerrequisitos + encontrar la IP real de cada máquina | Todas |
| 1 | Clonar repo + construir imagen + levantar contenedor + SSH | Todas |
| 2 | Generar llave SSH | Solo HOST |
| 3 | Copiar la llave pública del HOST a `authorized_keys` + verificar | HOST (a sí mismo) y cada trabajador |
| 4 | Probar SSH del HOST hacia cada trabajador | Solo HOST |
| 5 | Editar `hostfile` con las IPs reales | Solo HOST |
| 6 | Preparar el log (`touch` + `tail -f`) | Todas (antes de ejecutar) |
| 7 | Lanzar el reto con `./mpirun_lan.sh` | Solo HOST |

> Importante: los contenedores **trabajadores** deben estar corriendo con
> `sshd` ya iniciado (fin de la Fase 1) **antes** de que el HOST intente la
> Fase 4 o la Fase 7. Si un trabajador no está arriba, el HOST no puede
> conectarse a él.

---

## FASE 0 — Prerrequisitos y encontrar la IP real (en CADA computadora)

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

### Encontrar la IP real de la máquina en el router

```bash
ip -4 addr show | grep "inet " | grep -v "127.0.0.1"
```

Esto puede imprimir **varias** líneas. Hay que descartar:
- `127.0.0.1` (loopback, ya excluida por el comando).
- Cualquier `172.x.x.x` en interfaces `docker0` o `br-xxxxxxxxxxxx`
  — esas son redes **internas de Docker**, no sirven para que otra
  computadora te encuentre.

La IP correcta es la que sale por la interfaz real de red (`eth0`,
`wlan0`, `wlp2s0`, etc.), normalmente algo como `192.168.x.x` o `10.x.x.x`
dependiendo de cómo esté configurado el router.

> Atajo más directo (imprime solo la interfaz con salida a internet y su IP):
> ```bash
> ip route show default
> ip -4 -o addr show "$(ip route show default | awk '/default/ {print $5; exit}')" | awk '{print $4}'
> ```

Anota la IP de **cada** máquina y decide cuál será el **HOST (master)**.
Ejemplo real de esta guía: HOST `10.100.18.44`, trabajador `10.100.1.213`.

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

**¿Primera vez en esta máquina, o ya tenías la imagen construida y no cambió el código?**

- **Primera vez (o hiciste `git pull` con cambios en `parte_1/`, `hostfile` o
  el `Dockerfile`):** construir/reconstruir la imagen. Desde la **raíz** del
  repositorio:
  ```bash
  docker build -t mpi-cluster-image -f parte_2/Dockerfile .
  ```
  Si la imagen `mpi-cluster-image` ya existía, este comando simplemente la
  **reemplaza** con la versión nueva (no necesitas borrarla a mano).

- **Ya la tenías construida y no cambió el código:** puedes saltarte este
  paso por completo y pasar directo al 1.3.

> Para ver qué imágenes tienes: `docker images`

### 1.3 Levantar el contenedor y arrancar SSH

**¿Primera vez, o ya existe un contenedor `mpi_node` en esta máquina?**

Revisa primero qué tienes:
```bash
docker ps -a
```

- **No existe ningún contenedor `mpi_node` todavía:** créalo:
  ```bash
  docker run -it --network host --name mpi_node mpi-cluster-image /bin/bash
  service ssh start
  ```

- **Ya existe `mpi_node` y solo quieres volver a entrar (sin perder la
  llave SSH, el `hostfile` editado, etc. que ya tenías dentro):** reusa el
  mismo contenedor en vez de crear uno nuevo:
  ```bash
  docker start -ai mpi_node
  ```
  (si no te da una shell interactiva, usa en su lugar
  `docker start mpi_node` seguido de `docker exec -it mpi_node /bin/bash`)

  Luego, dentro, vuelve a arrancar SSH (esto **no** se conserva al
  reiniciar el contenedor):
  ```bash
  service ssh start
  ```

- **Ya existe `mpi_node` pero reconstruiste la imagen en el paso 1.2**
  (porque hubo cambios de código): el contenedor viejo sigue corriendo el
  código antiguo, así que hay que reemplazarlo:
  ```bash
  docker rm -f mpi_node
  docker run -it --network host --name mpi_node mpi-cluster-image /bin/bash
  service ssh start
  ```
  > Esto sí borra la llave SSH y el `hostfile` editado de ese contenedor:
  > tendrás que repetir la Fase 3 (llaves) y la Fase 5 (hostfile) en esta
  > máquina.

> En cualquier caso: déjalo abierto (no cierres esta terminal). El `sshd`
> del contenedor escucha en el puerto **2222**.

Para comprobar que quedó bien arrancado:
```bash
service ssh status
ss -tlnp | grep 2222
```
Debe decir `sshd is running` y mostrar una línea `LISTEN` en el puerto 2222.

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

### 3.3 Verificar que quedó bien (en cada trabajador)

```bash
cat /root/.ssh/authorized_keys        # debe mostrar la linea ssh-rsa completa, sin cortes
ls -la /root/.ssh/                    # .ssh en 700, authorized_keys en 600
service ssh status                    # "sshd is running"
ss -tlnp | grep 2222                  # debe mostrar un LISTEN en el puerto 2222
```

Si algo de esto falla, repite la Fase 3.2 (llave/permisos) o la Fase 1.3
(`service ssh start`) en ese trabajador antes de seguir.

---

## FASE 4 — Probar la conexión SSH (desde el HOST)

Dentro del contenedor del HOST, prueba entrar a cada trabajador con su IP real
(la que encontraron en la Fase 0):
```bash
ssh 10.100.1.213 hostname
```
Debe responder el hostname **sin pedir contraseña**. Si quieres ver el
detalle de la negociación (útil para diagnosticar), agrega `-v`:
```bash
ssh -v 10.100.1.213 hostname
```

Lectura de errores comunes:
- **`Connection refused` en el puerto 2222:** el trabajador no tiene
  `sshd` corriendo todavía → repite FASE 1.3 en ese trabajador.
- **Pide contraseña, o `Permission denied (publickey)`:** la llave no
  quedó bien puesta en ese trabajador → repite FASE 3.2 y 3.3.
- **`No route to host` / timeout:** revisa que ambas máquinas estén en la
  misma red (Fase 0) y que el firewall permita el puerto 2222.
- **Funciona y te muestra `Authenticated ... using "publickey"` y el
  hostname remoto:** ✅ todo bien, sigue a la Fase 5.

---

## FASE 5 — Configurar el hostfile (SOLO en el HOST)

Dentro del contenedor del HOST, edita el hostfile con las IPs **reales**
que encontraron en la Fase 0, el **HOST primero**:
```bash
nano hostfile
```
Ejemplo (estás en `/app`):
```
10.100.18.44 slots=1
10.100.1.213 slots=1
```
(agrega una línea más por cada trabajador adicional).

Prueba que MPI vea todos los nodos:
```bash
mpirun --hostfile hostfile -np 2 hostname
```
Debe imprimir el hostname de cada máquina (sin colgarse ni pedir contraseña).
Si esto se cuelga, no te preocupes todavía — la Fase 7 ya trae la solución
permanente para ese problema.

---

## FASE 6 — Preparar el log (en CADA máquina: HOST y trabajadores)

Antes de ejecutar el reto, en cada contenedor abre otra terminal y deja el
log corriendo:
```bash
touch /tmp/bruteforce.log
tail -f /tmp/bruteforce.log
```
> Ojo: es `/tmp`, no `/temp`. Si el archivo no existe al hacer `tail -f`,
> revisa que no haya un typo en la ruta.
>
> Esto es lo que te deja ver el progreso de **esa** máquina en tiempo real
> (el HOST solo muestra en pantalla la salida final combinada, no el avance
> de cada trabajador).

---

## FASE 7 — Ejecutar el reto (SOLO desde el HOST)

### Por qué NO usar `mpirun` directo

Si corres `mpirun` a secas, en algunos casos se **cuelga**: el contenedor
tiene varias interfaces internas de Docker (`docker0`, `br-xxxxxxxxxxxx`)
además de la IP real, y MPI puede intentar comunicarse por esas IPs
internas, que el otro nodo no puede alcanzar. Esto ya nos pasó probando
este clúster.

### Usar el wrapper `mpirun_lan.sh` (ya viene en la imagen)

El repo incluye `parte_2/mpirun_lan.sh`, horneado dentro de la imagen en
`/app/mpirun_lan.sh`. Detecta automáticamente la subred real de la
máquina (la de la interfaz con ruta por defecto) y limita MPI a ella, sin
tener que calcular ni escribir ningún CIDR a mano:

```bash
./mpirun_lan.sh -x BRUTEFORCE_LOG=/tmp/bruteforce.log \
                --hostfile hostfile -np 2 ./bruteforce_own 999999 1000000 texto.txt
```

Se usa exactamente igual que `mpirun`, solo cambia el nombre del comando.
Al arrancar imprime algo como:
```
[mpirun_lan] interfaz=wlp2s0 subred=10.100.18.44/19 (excluyendo interfaces de Docker)
```

> Si tu contenedor es de **antes** de que se agregara este script (no
> existe `/app/mpirun_lan.sh`), reconstruye la imagen y recrea el
> contenedor (ver FASE 1.2 y 1.3, rama "reconstruiste la imagen").

### Alternativa manual (si por algún motivo no quieres usar el wrapper)

```bash
mpirun --mca btl_tcp_if_include <SUBRED> --mca oob_tcp_if_include <SUBRED> \
       -x BRUTEFORCE_LOG=/tmp/bruteforce.log \
       --hostfile hostfile -np 2 ./bruteforce_own 999999 1000000 texto.txt
```
Donde `<SUBRED>` es la IP/máscara real de tu red (ver FASE 0), por ejemplo
`10.100.0.0/19` o `192.168.1.0/24`. Esto es exactamente lo que
`mpirun_lan.sh` hace por ti automáticamente.

---

La salida combinada de todos los procesos aparece **solo en la terminal
del HOST**, con el resumen `RESULT,llave,procesos,segundos,intentos`. El
progreso de cada máquina individual se ve en su propio
`tail -f /tmp/bruteforce.log` (FASE 6).

---

## Notas rápidas

- **Windows:** la red `host` de Docker Desktop **no sirve** para MPI (el
  contenedor solo ve IPs internas `192.168.65.x`). Usa Docker Engine dentro
  de WSL2 con `networkingMode=mirrored` en `%USERPROFILE%\.wslconfig`.
- Para los casos de "ya tenía la imagen/el contenedor creado, ¿qué hago?",
  ver las ramas explicadas en la **FASE 1.2 y 1.3** (reusar contenedor,
  reconstruir imagen, reemplazar contenedor).
- **Si la IP de alguna máquina cambió** (otra red, DHCP, etc.): **no** hay
  que reconstruir la imagen ni recrear el contenedor. Como se usa
  `--network host`, el contenedor comparte la IP de la máquina
  automáticamente. Solo hay que, en el HOST:
  1. Volver a ejecutar la Fase 0 para obtener la IP nueva.
  2. Editar el `hostfile` con la IP nueva (FASE 5).
  3. Volver a probar `ssh <ip-nueva> hostname` (FASE 4) — debería seguir
     funcionando sin pedir contraseña, porque la llave SSH no depende de
     la IP.
- **El colgón de `mpirun` por las interfaces de Docker** ya no debería
  repetirse: usa siempre `./mpirun_lan.sh` en vez de `mpirun` a secas
  (ver FASE 7).

## Prueba real ya hecha con este flujo

Con 2 máquinas (HOST `10.100.18.44` + trabajador `10.100.1.213`, llave
`999999`, límite `1000000`):
```
[mpirun_lan] interfaz=wlp2s0 subred=10.100.18.44/19 (excluyendo interfaces de Docker)
Procesos MPI: 2
Proceso 0 -> rango [0, 500000)
Proceso 1 -> rango [500000, 1000000)
Llave encontrada: 999999
RESULT,999999,2,37.13,1000000
```
Confirma que el flujo completo (llaves, hostfile, `mpirun_lan.sh`) funciona
real entre dos computadoras distintas en la misma red.
