# Proyecto 2 — DES-ECB y búsqueda MPI

El programa permite publicar nuestro reto, resolver un cifrado recibido o ejecutar la demostración original. Usa C11, OpenMPI y OpenSSL 3 (proveedores `legacy` y `default`).

## Estructura

- `parte_1/`: código, pruebas y benchmarks. Todos los comandos de las secciones 1 a 4 se ejecutan desde esta carpeta (`cd parte_1`).
- `parte_2/`: `Dockerfile` y `hostfile` para el clúster MPI entre varias computadoras (ver sección 5).

## Compilar en Linux o Ubuntu/WSL

```bash
make
# Equivalente, sin Make:
mpicc -std=c11 -O2 -Wall -Wextra -Wpedantic bruteforce_own.c -o bruteforce_own -lcrypto
```

Si faltan dependencias en Ubuntu:

```bash
sudo apt update
sudo apt install build-essential openmpi-bin libopenmpi-dev libssl-dev python3
```

Desde Windows, abrir Ubuntu/WSL y entrar a `/mnt/c/Projects/Proyecto2_Paralela/parte_1`.

## 1. Publicar nuestro reto

Solo cifra: no ejecuta fuerza bruta ni imprime la llave. Usar una ejecución con un proceso.

```bash
mpirun -np 1 ./bruteforce_own encrypt --key 50000 --input texto.txt \
  --target "pan de banano rico y saludable" --format hex --output texto_cifrado.txt
```

Para un foro que solicita Base64:

```bash
mpirun -np 1 ./bruteforce_own encrypt --key 50000 --input texto.txt \
  --target "pan de banano rico y saludable" --format base64 --output texto_cifrado_base64.txt
```

Publicar el contenido del archivo y la frase `pan de banano rico y saludable`. No publicar el comando anterior, que contiene la llave, ni los resultados privados de búsqueda. Ambos formatos representan exactamente los mismos bytes cifrados. El archivo contiene solamente el cifrado, sin llave, encabezados ni texto original.

`--format binary` genera bytes crudos; su archivo por defecto es `texto_cifrado.bin`. Los archivos de salida indicados se reemplazan al ejecutar. Guardar los archivos recibidos con nombres distintos de los propios.

La frase indicada debe aparecer en el archivo original; de lo contrario el programa rechaza la generación. Las tildes, mayúsculas y espacios deben coincidir. Para publicar otro texto, cambiar `--input`, `--key` y `--target`.

## 2. Resolver el reto de otro equipo

Guardar el cifrado recibido en `recibido_base64.txt`, sin copiar las etiquetas del foro. Este archivo debe contener únicamente Base64. Se admiten espacios, saltos de línea y BOM UTF-8. No se aceptan archivos binarios envueltos en un contenedor ni encabezados de `openssl enc` con salt.

```bash
mpirun -np 1 ./bruteforce_own crack --input recibido_base64.txt --format base64 \
  --target "frase exacta del otro equipo" --upper 1000000 --output texto_recuperado.txt
```

Para hexadecimal, usar `--format hex`; para bytes crudos, `--format binary`. No es necesario editar el código ni conocer o suministrar la llave. El modo `crack` nunca cifra de nuevo el archivo recibido ni genera `texto_cifrado.txt`.

`--upper N` busca las llaves numéricas en `[0, N)`. El valor 1000000 es un ejemplo de rango de prueba, no una garantía de que la llave ajena esté allí. El máximo permitido es `72057594037927936` (2^56). Si no se encuentra una coincidencia, comprobar la frase y la compatibilidad antes de ampliar el rango.

El programa imprime la llave encontrada, el tiempo, los intentos y el texto recuperado; también guarda el texto en el archivo indicado. Comprueba que la frase esté en el descifrado, pero una frase corta no certifica por sí sola todo el mensaje: revisar que el texto completo sea coherente y pedir confirmación al equipo si hace falta.

### Compatibilidad del cifrado recibido

Todos los modos usan DES-ECB. Base64 y hexadecimal son representaciones de bytes, no algoritmos de cifrado. Deben coincidir además la conversión de la llave y el padding:

| Opción | Comportamiento |
|---|---|
| `--padding zero` (por defecto) | Relleno con ceros hasta múltiplo de 8; quita ceros finales al recuperar |
| `--padding pkcs7` | Agrega y valida padding PKCS#7, incluido un bloque completo cuando corresponde |
| `--padding none` | Sin relleno; el texto original debe tener una longitud múltiplo de 8 |

El proyecto conserva su conversión numérica original a llave DES con paridad impar; ahora la disposición de bytes es explícitamente little-endian para que coincida entre máquinas. No equivale a interpretar directamente el número como una llave hexadecimal. Una llave DES arbitraria de otro programa puede requerir adaptar esta conversión. DES-CBC u otros modos tampoco son compatibles con DES-ECB.

`zero` no conserva ceros originales al final de un archivo binario; para textos habituales no supone pérdida. Para datos arbitrarios, acordar PKCS#7 con el otro equipo.

## 3. Tiempo base, speedup y eficiencia

La referencia secuencial `Ts` se mide en una sola máquina con **un proceso**. Después se mide `Tp` con varios procesos usando el mismo archivo, frase, rango, padding y versión del programa.

```bash
python3 benchmark_recibido.py --input recibido_base64.txt --format base64 \
  --target "frase exacta del otro equipo" --upper 1000000 \
  --processes 1 2 4 --repetitions 3 --output-dir mediciones_recibido
```

El script siempre ejecuta primero el caso de un proceso, aunque no se incluya en `--processes`. Calcula `Ts` como la media de esas repeticiones y guarda:

- `resultados.csv`: parámetros del reto, procesos, repetición, llave encontrada, tiempo, intentos, Ts medio, speedup y eficiencia.
- Un `.log` completo por ejecución y su archivo de texto recuperado.
- En consola, el tiempo medio y el speedup calculado sobre medias para cada cantidad de procesos.

Cada fila usa `speedup = Ts_medio / tiempo_de_la_fila` y `eficiencia_pct = 100 * speedup / procesos`. Por variación entre repeticiones, las filas individuales de un proceso pueden diferir de 1; el speedup sobre medias para un proceso es 1. El script se detiene si falla una búsqueda, cambia la llave o cambia el texto recuperado. La carpeta de salida debe ser nueva para conservar experimentos anteriores.

Para un clúster se puede agregar `--hostfile hosts.txt`; se aplica solo a las pruebas con más de un proceso, manteniendo Ts local. Las rutas absolutas del ejecutable, entrada y carpeta de resultados deben estar disponibles en los nodos (por ejemplo, en un directorio compartido), con la misma compilación y entorno MPI. Elegir cantidades de procesos compatibles con los slots disponibles.

El tiempo registrado comienza después de distribuir el archivo y sincronizar los procesos. Incluye descifrar candidatos, buscar la frase y comprobar colectivamente la terminación; excluye la lectura, conversión, cifrado inicial y escritura del resultado. Se informa el máximo tiempo de los procesos.

La búsqueda distribuye rangos contiguos equilibrados y verifica la terminación cada 1024 candidatos. Usa colectivas MPI para evitar bloqueos por avisos simultáneos. Las ejecuciones nuevas deben compararse entre sí: el mecanismo de terminación cambió respecto de la versión anterior. Al detenerse cuando aparece una coincidencia, el trabajo realizado puede variar con el reparto; interpretar el speedup junto con los intentos y las repeticiones.

## 4. Demostración y benchmark original

Se conserva el comando anterior, sus valores por defecto y la salida CSV `RESULT,llave,procesos,segundos,intentos` que consume `benchmark.sh`:

```bash
mpirun -np 4 ./bruteforce_own 625000 1000000 texto.txt
# Sin argumentos: llave 50000, limite 1000000, texto.txt.
```

También se puede usar un comando explícito:

```bash
mpirun -np 4 ./bruteforce_own demo --key 625000 --upper 1000000 \
  --input texto.txt --target "pan de banano rico y saludable"
```

La demostración cifra, guarda `texto_cifrado.txt`, busca la llave y guarda `texto_recuperado.txt`. Solo rank 0 lee y escribe archivos. En demo el primer campo de `RESULT` sigue siendo la llave original para mantener el benchmark; en `crack` es la llave encontrada.

```bash
bash benchmark.sh
```

`benchmark.sh` sigue midiendo retos propios. Para el reto ajeno usar `benchmark_recibido.py`. El registro de demostración contiene llaves y texto plano; no publicarlo como archivo cifrado.

Códigos de salida: `0` éxito, `1` argumentos/archivo/cifrado inválidos o fallo de escritura, `2` búsqueda terminada sin coincidencias. Sin coincidencias se emite `NOT_FOUND,procesos,segundos,intentos`, sin una fila `RESULT` de éxito ni un texto nuevo recuperado. Un archivo recuperado de una ejecución previa no se elimina automáticamente.

## Organización y pruebas

`bruteforce_own.c` conserva una sola unidad de compilación para mantener el comando original. Se organiza en opciones validadas, lectura y codificación, operaciones DES, búsqueda MPI y resultados. Los datos se agrupan en `Options`, `Buffer`, `Crypto` y `SearchResult`; cada proceso tiene su contexto DES y un buffer reutilizable, sin variables globales mutables ni buffers de texto de tamaño variable en la pila.

```bash
make test
# O, despues de compilar:
python3 tests/test_workflow.py
```

Las pruebas ejecutan MPI real con 1 y 4 procesos: recuperación en los tres formatos y paddings, ciphertext externo producido por OpenSSL, compatibilidad con el cifrado ya publicado, demostración antigua, rangos vacíos, varias rondas de búsqueda, llave cero, ausencia de coincidencia y rechazo de entradas inválidas. Trabajan en carpetas temporales y no reemplazan el reto publicado.

Para ver la ayuda:

```bash
./bruteforce_own --help
```

## 5. Clúster con Docker

Construir la imagen desde la raíz del repositorio (copia y compila `parte_1/` dentro de la imagen):

```bash
docker build -t mpi-cluster-image -f parte_2/Dockerfile .
```

En cada computadora conectada al mismo router:

```bash
docker run -it --network host --name mpi_node mpi-cluster-image /bin/bash
service ssh start
```

El SSH del contenedor escucha en el puerto `2222` para no chocar con el SSH del host. La llave pública de quien ejecuta `mpirun` debe estar en `~/.ssh/authorized_keys` de todos los contenedores. El `hostfile` viene en `/app/hostfile` dentro del contenedor: en el master, cambiar las IPs de ejemplo por las reales (`nano hostfile`), poniendo primero la del master:

```bash
mpirun --hostfile hostfile -np 3 hostname
mpirun --hostfile hostfile -np 3 ./bruteforce_own 999999 1000000 texto.txt
```
