"""Integration tests with real OpenMPI and OpenSSL 3; no extra Python packages."""
import base64
import csv
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = ROOT / "bruteforce_own"
TARGET = "pan de banano rico y saludable"


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.text = self.folder / "plain.txt"
        self.plain = ("Texto UTF-8: computación; " + TARGET + ". Fin.").encode()
        self.text.write_bytes(self.plain)

    def run_mpi(self, *args, processes=1, expected=0):
        env = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT="1", OMPI_ALLOW_RUN_AS_ROOT_CONFIRM="1")
        completed = subprocess.run(
            ["mpirun", "--oversubscribe", "-np", str(processes), str(EXECUTABLE), *map(str, args)],
            cwd=self.folder, capture_output=True, timeout=45, env=env)
        self.assertEqual(completed.returncode, expected,
                         completed.stdout.decode(errors="replace") + completed.stderr.decode(errors="replace"))
        return completed.stdout.decode("utf-8")

    def encrypt(self, fmt="hex", padding="zero", key=7):
        output = self.folder / f"cipher_{fmt}_{padding}.txt"
        log = self.run_mpi("encrypt", "--key", key, "--input", self.text,
                           "--target", TARGET, "--format", fmt, "--padding", padding,
                           "--output", output, processes=4)
        self.assertEqual(log.count("Archivo cifrado generado:"), 1)
        self.assertNotIn("Llave", log)
        return output

    def crack(self, path, fmt, *, padding="zero", processes=1, target=TARGET,
              upper=32, expected=0):
        output = self.folder / "recovered.txt"
        log = self.run_mpi("crack", "--input", path, "--format", fmt, "--target", target,
                           "--upper", upper, "--padding", padding, "--output", output,
                           processes=processes, expected=expected)
        if expected == 0:
            self.assertEqual(output.read_bytes(), self.plain)
            self.assertRegex(log, r"(?m)^RESULT,7," + str(processes) + r",[0-9.]+,\d+$")
        return log

    def test_roundtrip_formats_and_padding(self):
        for padding in ("zero", "pkcs7", "none"):
            if padding == "none":
                self.plain += b" " * (-len(self.plain) % 8)
                self.text.write_bytes(self.plain)
            expected_cipher = None
            for fmt in ("hex", "base64", "binary"):
                with self.subTest(padding=padding, format=fmt):
                    path = self.encrypt(fmt, padding)
                    raw = path.read_bytes()
                    decoded = bytes.fromhex(raw.decode()) if fmt == "hex" else (
                        base64.b64decode(raw, validate=True) if fmt == "base64" else raw)
                    self.assertEqual(len(decoded) % 8, 0)
                    if expected_cipher is None:
                        expected_cipher = decoded
                    self.assertEqual(decoded, expected_cipher)
                    for processes in (1, 4):
                        self.crack(path, fmt, padding=padding, processes=processes)
                    self.assertEqual(path.read_bytes(), raw)

    def test_preserves_previous_published_cipher(self):
        output = self.folder / "original.hex"
        self.run_mpi("encrypt", "--key", 50000, "--input", ROOT / "texto.txt",
                     "--output", output)
        digest = hashlib.sha256(bytes.fromhex(output.read_text())).hexdigest()
        self.assertEqual(digest, "d78e9ec6e038643499b1202cff3c807dbebe492d5a3ad33e60d3d41137debe03")

    def test_legacy_and_default_demo(self):
        log = self.run_mpi(7, 32, self.text, processes=4)
        self.assertRegex(log, r"(?m)^RESULT,7,4,[0-9.]+,\d+$")
        self.assertEqual((self.folder / "texto_recuperado.txt").read_bytes(), self.plain)
        self.assertTrue(re.fullmatch(r"[0-9A-F\n]+", (self.folder / "texto_cifrado.txt").read_text()))
        (self.folder / "texto.txt").write_bytes(self.plain)
        self.run_mpi()
        self.assertEqual((self.folder / "texto_recuperado.txt").read_bytes(), self.plain)

    def test_no_match_and_empty_rank_ranges(self):
        path = self.encrypt()
        self.crack(path, "hex", processes=4, target="frase inexistente", expected=2)
        self.crack(path, "hex", processes=4, target="frase inexistente", upper=4101, expected=2)
        self.assertFalse((self.folder / "recovered.txt").exists())
        self.crack(path, "hex", processes=4, upper=1, expected=2)
        # Key zero is valid; ranks with empty ranges still join collectives.
        path = self.encrypt(key=0)
        log = self.run_mpi("crack", "--input", path, "--target", TARGET,
                           "--upper", 1, processes=4)
        self.assertIn("Llave encontrada: 0", log)

    def test_multiple_search_batches(self):
        path = self.encrypt(key=1025)
        log = self.run_mpi("crack", "--input", path, "--target", TARGET,
                           "--upper", 1100, processes=1)
        self.assertIn("Llave encontrada: 1025", log)
        self.assertEqual((self.folder / "texto_recuperado.txt").read_bytes(), self.plain)

    def test_malformed_input_and_arguments(self):
        for fmt, raw in (("hex", b"A"), ("hex", b"GG" * 8), ("hex", b"00" * 7),
                         ("base64", b"===="), ("base64", b"AB=="),
                         ("base64", b"YWJj$==="), ("binary", b"1234567"), ("hex", b"")):
            with self.subTest(format=fmt, raw=raw):
                bad = self.folder / "bad.txt"
                bad.write_bytes(raw)
                self.crack(bad, fmt, processes=4, expected=1)
        for upper in ("-1", "0", "12oops", str((1 << 56) + 1)):
            self.run_mpi("crack", "--input", self.text, "--target", TARGET,
                         "--upper", upper, expected=1)
        self.run_mpi("crack", "--input", self.text, "--upper", 32, expected=1)
        self.run_mpi("encrypt", "--input", self.text, "--key", 7,
                     "--target", "inexistente", expected=1)
        self.assertFalse((self.folder / "texto_cifrado.txt").exists())

    def test_external_openssl_cipher(self):
        # Numeric key 7 maps to DES bytes 0E01010101010101 in our project.
        padded = self.plain + b"\0" * (-len(self.plain) % 8)
        external = subprocess.run(
            ["openssl", "enc", "-des-ecb", "-provider", "legacy", "-provider", "default",
             "-K", "0E01010101010101", "-nopad"],
            input=padded, capture_output=True, check=True).stdout
        path = self.folder / "external.b64"
        path.write_bytes(b"\xef\xbb\xbf" + b"\n".join(base64.b64encode(external)[i:i+20]
                                      for i in range(0, len(base64.b64encode(external)), 20)))
        self.crack(path, "base64", processes=4)

    def test_received_benchmark(self):
        path = self.encrypt("base64")
        output = self.folder / "measurements"
        completed = subprocess.run(
            ["python3", str(ROOT / "benchmark_recibido.py"), "--input", str(path),
             "--format", "base64", "--target", TARGET, "--upper", "32",
             "--processes", "1", "2", "--repetitions", "2",
             "--executable", str(EXECUTABLE), "--output-dir", str(output)],
            cwd=self.folder, capture_output=True, timeout=45,
            env=dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT="1", OMPI_ALLOW_RUN_AS_ROOT_CONFIRM="1"))
        self.assertEqual(completed.returncode, 0, completed.stderr.decode(errors="replace"))
        with (output / "resultados.csv").open() as file:
            rows = list(csv.DictReader(file))
        self.assertEqual(len(rows), 4)
        ts = sum(float(row["tiempo_s"]) for row in rows[:2]) / 2
        for row in rows:
            self.assertEqual(row["llave"], "7")
            self.assertEqual(Path(row["texto_recuperado"]).read_bytes(), self.plain)
            self.assertTrue(Path(row["log"]).exists())
            self.assertAlmostEqual(float(row["speedup"]), ts / float(row["tiempo_s"]), places=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
