#!/usr/bin/env python3
"""Measure one received challenge; save raw MPI logs and a comparable CSV."""
import argparse
import csv
from pathlib import Path
import re
import statistics
import subprocess


def positive(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("Debe ser mayor que cero")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--format", choices=["hex", "base64", "binary"], required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--upper", type=positive, required=True)
    parser.add_argument("--padding", choices=["zero", "pkcs7", "none"], default="zero")
    parser.add_argument("--processes", type=positive, nargs="+", default=[1, 2, 4])
    parser.add_argument("--repetitions", type=positive, default=3)
    parser.add_argument("--executable", default="./bruteforce_own")
    parser.add_argument("--hostfile", help="Hostfile MPI opcional para procesos > 1")
    parser.add_argument("--output-dir", default="mediciones_recibido")
    args = parser.parse_args()
    if args.upper > 1 << 56:
        parser.error("--upper no puede superar 2^56")
    if not args.target:
        parser.error("La frase objetivo no puede estar vacia")
    output = Path(args.output_dir).resolve()
    # Never silently overwrite an earlier experiment.
    output.mkdir(parents=True, exist_ok=False)
    source = str(Path(args.input).resolve())
    executable = str(Path(args.executable).resolve())
    counts = [1] + sorted(set(args.processes) - {1})
    pattern = re.compile(r"^RESULT,(\d+),(\d+),([0-9.]+),(\d+)$", re.MULTILINE)
    baseline = None
    found_key = None
    reference_plaintext = None
    fields = ["archivo", "formato", "objetivo", "padding", "limite", "procesos",
              "repeticion", "llave", "tiempo_s", "intentos", "ts_medio_s",
              "speedup", "eficiencia_pct", "log", "texto_recuperado"]
    with (output / "resultados.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for count in counts:
            samples = []
            group = []
            for run in range(1, args.repetitions + 1):
                stem = f"p{count}_r{run}"
                recovered = output / f"{stem}_recuperado.txt"
                log = output / f"{stem}.log"
                command = ["mpirun", "-np", str(count)]
                if args.hostfile and count > 1:
                    command += ["--hostfile", args.hostfile]
                command += [executable, "crack", "--input", source,
                            "--format", args.format, "--target", args.target,
                            "--upper", str(args.upper), "--padding", args.padding,
                            "--output", str(recovered)]
                print(f"Procesos: {count}; repeticion: {run}", flush=True)
                completed = subprocess.run(command, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT)
                log.write_bytes(completed.stdout)
                matches = pattern.findall(completed.stdout.decode("utf-8", errors="replace"))
                if completed.returncode or len(matches) != 1 or not recovered.exists():
                    raise SystemExit(f"Prueba fallida; revise {log}. No se calcula speedup.")
                key, processes, seconds, attempts = matches[0]
                if int(processes) != count:
                    raise SystemExit(f"Cantidad de procesos inesperada; revise {log}")
                if found_key is None:
                    found_key = key
                if key != found_key:
                    raise SystemExit("Las ejecuciones encontraron llaves diferentes; revise los logs.")
                plaintext = recovered.read_bytes()
                if args.target.encode("utf-8") not in plaintext:
                    raise SystemExit(f"No se verifico la frase objetivo en {recovered}")
                if reference_plaintext is None:
                    reference_plaintext = plaintext
                if plaintext != reference_plaintext:
                    raise SystemExit("El texto recuperado cambio entre ejecuciones.")
                elapsed = float(seconds)
                if elapsed <= 0:
                    raise SystemExit("Tiempo demasiado corto para calcular speedup.")
                samples.append(elapsed)
                group.append(dict(archivo=source, formato=args.format, objetivo=args.target,
                                  padding=args.padding, limite=args.upper, procesos=count,
                                  repeticion=run, llave=key, tiempo_s=seconds, intentos=attempts,
                                  log=str(log), texto_recuperado=str(recovered)))
            mean = statistics.mean(samples)
            if count == 1:
                baseline = mean
            for row in group:
                speedup = baseline / float(row["tiempo_s"])
                row.update(ts_medio_s=f"{baseline:.9f}", speedup=f"{speedup:.6f}",
                           eficiencia_pct=f"{100 * speedup / count:.4f}")
                writer.writerow(row)
            stream.flush()
            print(f"Media: {mean:.9f} s; speedup sobre medias: {baseline / mean:.4f}; "
                  f"eficiencia: {100 * baseline / mean / count:.2f}%", flush=True)
    print(f"Resultados: {output / 'resultados.csv'}")


if __name__ == "__main__":
    main()
