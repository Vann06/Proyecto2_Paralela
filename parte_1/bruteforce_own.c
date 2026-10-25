/* DES-ECB / OpenSSL 3 / MPI.
 * Keep this translation unit self-contained: mpicc ... -lcrypto.
 * Responsibilities: options, file codecs, DES, MPI search, presentation.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <openssl/evp.h>
#include <openssl/provider.h>

#define KEY_SPACE (1ULL << 56)
#define SEARCH_BATCH 1024ULL
#define DEFAULT_TARGET "pan de banano rico y saludable"

typedef enum { MODE_DEMO, MODE_ENCRYPT, MODE_CRACK } Mode;
typedef enum { FORMAT_HEX, FORMAT_BASE64, FORMAT_BINARY } Format;
typedef enum { PAD_ZERO, PAD_PKCS7, PAD_NONE } Padding;
typedef struct {
    Mode mode;
    Format format;
    Padding padding;
    unsigned long long key, upper;
    const char *input, *output, *target;
} Options;
typedef struct { unsigned char *data; int len; } Buffer;
typedef struct {
    OSSL_PROVIDER *legacy, *standard;
    EVP_CIPHER *des;
    EVP_CIPHER_CTX *ctx;
} Crypto;
typedef struct {
    unsigned long long key, attempts;
    double seconds;
} SearchResult;

static void usage(void)
{
    puts("Uso:\n"
         "  bruteforce_own [llave [limite [texto.txt]]]  (demo compatible)\n"
         "  bruteforce_own demo [opciones]\n"
         "  bruteforce_own encrypt --key N --input texto.txt [opciones]\n"
         "  bruteforce_own crack --input recibido.txt --format hex|base64|binary\n"
         "      --target \"frase exacta\" --upper N [opciones]\n\n"
         "Opciones:\n"
         "  --output ARCHIVO   Cifrado en encrypt; texto recuperado en crack/demo\n"
         "  --target FRASE     Por defecto en demo/encrypt: " DEFAULT_TARGET "\n"
         "  --format FORMATO   Por defecto: hex\n"
         "  --padding TIPO     zero (por defecto), pkcs7 o none\n"
         "  --key N            Llave numerica DES de nuestro proyecto\n"
         "  --upper N          Buscar en [0, N); maximo 2^56\n\n"
         "encrypt solo cifra; crack recibe ciphertext sin volver a cifrarlo.\n"
         "RESULT,llave,procesos,segundos,intentos mantiene el formato del benchmark.");
}

static int parse_number(const char *text, unsigned long long *value)
{
    char *end;
    if(!text || !*text) return 0;
    for(const char *p = text; *p; ++p)
        if(*p < '0' || *p > '9') return 0;
    errno = 0;
    *value = strtoull(text, &end, 10);
    return errno == 0 && *end == '\0';
}

static int parse_options(int argc, char **argv, Options *o)
{
    *o = (Options){MODE_DEMO, FORMAT_HEX, PAD_ZERO, 50000ULL,
                   1000000ULL, "texto.txt", NULL, DEFAULT_TARGET};
    if(argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")))
        return 2;
    if(argc == 1) {
        o->output = "texto_recuperado.txt";
        return 1;
    }
    if(isdigit((unsigned char)argv[1][0])) {
        if(argc > 4 || !parse_number(argv[1], &o->key)) return 0;
        if(argc >= 3 && !parse_number(argv[2], &o->upper)) return 0;
        if(argc >= 4) o->input = argv[3];
    } else {
        int has_key = 0, has_input = 0, has_target = 0, has_upper = 0;
        if(!strcmp(argv[1], "encrypt")) o->mode = MODE_ENCRYPT;
        else if(!strcmp(argv[1], "crack")) o->mode = MODE_CRACK;
        else if(strcmp(argv[1], "demo")) return 0;
        for(int i = 2; i < argc; i += 2) {
            if(i + 1 == argc) return 0;
            const char *name = argv[i], *value = argv[i + 1];
            if(!strcmp(name, "--key")) {
                if(!parse_number(value, &o->key)) return 0;
                has_key = 1;
            } else if(!strcmp(name, "--upper")) {
                if(!parse_number(value, &o->upper)) return 0;
                has_upper = 1;
            } else if(!strcmp(name, "--input")) {
                o->input = value; has_input = 1;
            } else if(!strcmp(name, "--output")) o->output = value;
            else if(!strcmp(name, "--target")) {
                o->target = value; has_target = 1;
            } else if(!strcmp(name, "--format")) {
                if(!strcmp(value, "hex")) o->format = FORMAT_HEX;
                else if(!strcmp(value, "base64")) o->format = FORMAT_BASE64;
                else if(!strcmp(value, "binary")) o->format = FORMAT_BINARY;
                else return 0;
            } else if(!strcmp(name, "--padding")) {
                if(!strcmp(value, "zero")) o->padding = PAD_ZERO;
                else if(!strcmp(value, "pkcs7")) o->padding = PAD_PKCS7;
                else if(!strcmp(value, "none")) o->padding = PAD_NONE;
                else return 0;
            } else return 0;
        }
        if(o->mode == MODE_CRACK && (!has_input || !has_target || !has_upper || has_key))
            return 0;
        if(o->mode == MODE_ENCRYPT && (!has_key || !has_input)) return 0;
        if(o->mode == MODE_DEMO && o->format != FORMAT_HEX) return 0;
    }
    if(!*o->target || strlen(o->target) > INT_MAX) return 0;
    if(o->mode != MODE_ENCRYPT && (o->upper == 0 || o->upper > KEY_SPACE)) return 0;
    if(o->mode != MODE_CRACK && o->key >= KEY_SPACE) return 0;
    if(o->mode == MODE_DEMO && o->key >= o->upper) return 0;
    if(o->mode == MODE_ENCRYPT && !o->output)
        o->output = o->format == FORMAT_HEX ? "texto_cifrado.txt" :
                    o->format == FORMAT_BASE64 ? "texto_cifrado_base64.txt" : "texto_cifrado.bin";
    if(o->mode != MODE_ENCRYPT && !o->output) o->output = "texto_recuperado.txt";
    if(!strcmp(o->input, o->output)) return 0;
    if(o->mode == MODE_DEMO && (!strcmp(o->input, "texto_cifrado.txt") ||
                               !strcmp(o->output, "texto_cifrado.txt"))) return 0;
    return 1;
}

/* Bounded binary I/O: MPI counts and EVP lengths are int. */
static int read_file(const char *name, Buffer *b)
{
    FILE *file = fopen(name, "rb");
    long size;
    if(!file) return 0;
    if(fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || size > INT_MAX - 8 ||
       fseek(file, 0, SEEK_SET)) { fclose(file); return 0; }
    b->data = malloc((size_t)size + 9);
    if(!b->data) { fclose(file); return 0; }
    size_t count = fread(b->data, 1, (size_t)size, file);
    int ok = !ferror(file) && count == (size_t)size;
    if(fclose(file)) ok = 0;
    if(!ok) { free(b->data); b->data = NULL; return 0; }
    b->len = (int)size;
    return 1;
}

static int write_bytes(const char *name, const unsigned char *data, int len)
{
    FILE *file = fopen(name, "wb");
    if(!file) return 0;
    int ok = fwrite(data, 1, (size_t)len, file) == (size_t)len;
    if(fclose(file)) ok = 0;
    return ok;
}

int save_cipher_hex(const char *filename, const unsigned char *cipher, int len)
{
    FILE *file = fopen(filename, "w");
    if(!file) return 0;
    int ok = 1;
    for(int i = 0; i < len && ok; ++i) {
        if(fprintf(file, "%02X", (unsigned int)cipher[i]) < 0) ok = 0;
        if(((i + 1) % 32 == 0 || i == len - 1) && fputc('\n', file) == EOF) ok = 0;
    }
    if(fclose(file)) ok = 0;
    return ok;
}

static int save_cipher(const char *name, const Buffer *b, Format format)
{
    if(format == FORMAT_HEX) return save_cipher_hex(name, b->data, b->len);
    if(format == FORMAT_BINARY) return write_bytes(name, b->data, b->len);
    size_t size = 4 * (((size_t)b->len + 2) / 3);
    if(size > INT_MAX) return 0;
    unsigned char *encoded = malloc(size + 1);
    if(!encoded) return 0;
    int len = EVP_EncodeBlock(encoded, b->data, b->len);
    int ok = len == (int)size && write_bytes(name, encoded, len);
    free(encoded);
    return ok;
}

static int hex_digit(unsigned char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int load_cipher(const Options *o, Buffer *b)
{
    if(!read_file(o->input, b)) return 0;
    if(o->format != FORMAT_BINARY) {
        int n = 0;
        /* Accept UTF-8 text saved by editors with a byte-order mark. */
        int start = b->len >= 3 && b->data[0] == 0xEF &&
                    b->data[1] == 0xBB && b->data[2] == 0xBF ? 3 : 0;
        for(int i = start; i < b->len; ++i)
            if(!isspace(b->data[i])) b->data[n++] = b->data[i];
        if(o->format == FORMAT_HEX) {
            if(n % 2) return 0;
            for(int i = 0; i < n; i += 2) {
                int a = hex_digit(b->data[i]), c = hex_digit(b->data[i + 1]);
                if(a < 0 || c < 0) return 0;
                b->data[i / 2] = (unsigned char)((a << 4) | c);
            }
            b->len = n / 2;
        } else {
            if(n == 0 || n % 4) return 0;
            int padding = (b->data[n - 1] == '=') + (b->data[n - 2] == '=');
            for(int i = 0; i < n; ++i) {
                unsigned char c = b->data[i];
                int valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '+' || c == '/';
                if(!valid && !(c == '=' && i >= n - padding)) return 0;
            }
            unsigned char *decoded = malloc((size_t)n + 9);
            if(!decoded) return 0;
            int len = EVP_DecodeBlock(decoded, b->data, n);
            if(len < padding) { free(decoded); return 0; }
            len -= padding;
            unsigned char *canonical = malloc((size_t)n + 1);
            if(!canonical) { free(decoded); return 0; }
            int canonical_len = EVP_EncodeBlock(canonical, decoded, len);
            int valid = canonical_len == n && memcmp(canonical, b->data, (size_t)n) == 0;
            free(canonical);
            if(!valid) { free(decoded); return 0; }
            free(b->data);
            b->data = decoded;
            b->len = len;
        }
    }
    return b->len > 0 && b->len % 8 == 0;
}

/* Original numeric-key mapping, now explicitly little-endian on every host. */
static void build_des_key(unsigned long long key, unsigned char des_key[8])
{
    unsigned long long k = 0;
    for(int i = 0; i < 8; ++i) {
        key <<= 1;
        k |= key & (0xFEULL << (i * 8));
    }
    for(int i = 0; i < 8; ++i) {
        unsigned char b = (unsigned char)(k >> (i * 8)) & 0xFE;
        des_key[i] = b | ((__builtin_popcount((unsigned int)b) % 2) == 0);
    }
}

static int crypto_init(Crypto *c)
{
    c->legacy = OSSL_PROVIDER_load(NULL, "legacy");
    c->standard = OSSL_PROVIDER_load(NULL, "default");
    c->des = EVP_CIPHER_fetch(NULL, "DES-ECB", NULL);
    c->ctx = EVP_CIPHER_CTX_new();
    return c->legacy && c->standard && c->des && c->ctx;
}

static void crypto_free(Crypto *c)
{
    EVP_CIPHER_CTX_free(c->ctx);
    EVP_CIPHER_free(c->des);
    OSSL_PROVIDER_unload(c->legacy);
    OSSL_PROVIDER_unload(c->standard);
}

static int des_ecb(Crypto *c, unsigned long long key, unsigned char *data, int len, int enc)
{
    unsigned char des_key[8];
    int out_len = 0, final_len = 0;
    build_des_key(key, des_key);
    return EVP_CipherInit_ex2(c->ctx, c->des, des_key, NULL, enc, NULL) == 1 &&
           EVP_CIPHER_CTX_set_padding(c->ctx, 0) == 1 &&
           EVP_CipherUpdate(c->ctx, data, &out_len, data, len) == 1 &&
           EVP_CipherFinal_ex(c->ctx, data + out_len, &final_len) == 1 &&
           out_len + final_len == len;
}

static int contains(const unsigned char *text, int len, const char *target)
{
    size_t size = strlen(target);
    if(size > (size_t)len) return 0;
    for(int i = 0; i <= len - (int)size; ++i)
        if(memcmp(text + i, target, size) == 0) return 1;
    return 0;
}

static int plaintext_length(const unsigned char *text, int len, Padding padding)
{
    if(padding == PAD_PKCS7) {
        int n = text[len - 1];
        if(n < 1 || n > 8 || n > len) return -1;
        for(int i = len - n; i < len; ++i) if(text[i] != n) return -1;
        return len - n;
    }
    if(padding == PAD_ZERO) while(len > 0 && text[len - 1] == 0) --len;
    return len;
}

static int prepare_cipher(Crypto *c, const Options *o, Buffer *b)
{
    if(!read_file(o->input, b) || b->len == 0) return 0;
    if(!contains(b->data, b->len, o->target)) {
        fprintf(stderr, "La frase objetivo no aparece en el texto original.\n");
        return 0;
    }
    int extra = o->padding == PAD_PKCS7 ? 8 - b->len % 8 : (8 - b->len % 8) % 8;
    if(o->padding == PAD_NONE && extra) return 0;
    memset(b->data + b->len, o->padding == PAD_PKCS7 ? extra : 0, (size_t)extra);
    b->len += extra;
    if(!des_ecb(c, o->key, b->data, b->len, 1)) return 0;
    const char *output = o->mode == MODE_DEMO ? "texto_cifrado.txt" : o->output;
    if(!save_cipher(output, b, o->format)) return 0;
    printf("Archivo cifrado generado: %s\nFormato: %s\nPalabra objetivo: %s\n",
           output, o->format == FORMAT_HEX ? "hexadecimal" :
           o->format == FORMAT_BASE64 ? "base64" : "binario", o->target);
    return 1;
}

/* Each rank owns its crypto context and one reusable candidate buffer. */
static int tryKey(Crypto *c, unsigned long long key, const Buffer *cipher,
                  unsigned char *scratch, const Options *o)
{
    memcpy(scratch, cipher->data, (size_t)cipher->len);
    if(!des_ecb(c, key, scratch, cipher->len, 0)) return -1;
    int len = plaintext_length(scratch, cipher->len, o->padding);
    return len >= 0 && contains(scratch, len, o->target);
}

/* Optional per-node progress log (mpirun -x BRUTEFORCE_LOG=path). Remote ranks'
 * stdout only reaches mpirun, so each node follows its own file with tail -f.
 */
static FILE *open_progress_log(int rank, unsigned long long first, unsigned long long end)
{
    const char *path = getenv("BRUTEFORCE_LOG");
    FILE *log = path && *path ? fopen(path, "a") : NULL;
    if(!log) return NULL;
    char host[MPI_MAX_PROCESSOR_NAME];
    int host_len;
    MPI_Get_processor_name(host, &host_len);
    setvbuf(log, NULL, _IOLBF, 0);
    fprintf(log, "\n[%s] Proceso %d: buscando llaves en [%llu, %llu)\n", host, rank, first, end);
    return log;
}

static SearchResult search_mpi(Crypto *c, const Options *o, Buffer *cipher,
                               int rank, int processes, MPI_Comm comm)
{
    unsigned char *scratch = malloc((size_t)cipher->len + 8);
    if(!scratch) MPI_Abort(comm, 1);
    unsigned long long q = o->upper / (unsigned long long)processes;
    unsigned long long r = o->upper % (unsigned long long)processes;
    unsigned long long first = q * (unsigned long long)rank +
                               ((unsigned long long)rank < r ? (unsigned long long)rank : r);
    unsigned long long count = q + ((unsigned long long)rank < r);
    unsigned long long end = first + count, next = first, attempts = 0;
    unsigned long long found = ULLONG_MAX, local = ULLONG_MAX;
    unsigned long long rounds = (q + (r != 0) + SEARCH_BATCH - 1) / SEARCH_BATCH;
    printf("Proceso %d -> rango [%llu, %llu)\n", rank, first, end);
    FILE *log = open_progress_log(rank, first, end);
    MPI_Barrier(comm);
    double start = MPI_Wtime(), last_report = start;
    /* All ranks enter every round, including ranks with an empty range.
     * One collective replaces unmatched stop messages and blocking sends.
     */
    for(unsigned long long round = 0; round < rounds; ++round) {
        for(unsigned long long n = 0; n < SEARCH_BATCH && next < end; ++n, ++next) {
            ++attempts;
            int match = tryKey(c, next, cipher, scratch, o);
            if(match < 0) MPI_Abort(comm, 1);
            if(match) { local = next; break; }
        }
        MPI_Allreduce(&local, &found, 1, MPI_UNSIGNED_LONG_LONG, MPI_MIN, comm);
        if(found != ULLONG_MAX) break;
        if(log && count && MPI_Wtime() - last_report >= 1.0) {
            last_report = MPI_Wtime();
            fprintf(log, "  Proceso %d: %llu/%llu llaves probadas (%.1f%%), ultima %llu\n",
                    rank, attempts, count, 100.0 * (double)attempts / (double)count, next - 1);
        }
    }
    double elapsed = MPI_Wtime() - start;
    if(log) {
        if(local != ULLONG_MAX)
            fprintf(log, "  Proceso %d: LLAVE ENCONTRADA: %llu\n", rank, local);
        else if(found != ULLONG_MAX)
            fprintf(log, "  Proceso %d: otro proceso encontro la llave %llu, me detengo\n", rank, found);
        else
            fprintf(log, "  Proceso %d: la llave no esta en mi rango\n", rank);
        fprintf(log, "  Proceso %d: %llu intentos en %.3f segundos\n", rank, attempts, elapsed);
        fclose(log);
    }
    SearchResult result = {found, 0, 0.0};
    MPI_Reduce(&elapsed, &result.seconds, 1, MPI_DOUBLE, MPI_MAX, 0, comm);
    MPI_Reduce(&attempts, &result.attempts, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, comm);
    free(scratch);
    return result;
}

static int report_result(Crypto *c, const Options *o, Buffer *cipher,
                          const SearchResult *result, int processes)
{
    printf("Procesos MPI: %d\nIntentos totales: %llu\nTiempo busqueda: %.9f segundos\n",
           processes, result->attempts, result->seconds);
    if(result->key == ULLONG_MAX) {
        puts("No se encontro la llave en el rango indicado.");
        printf("NOT_FOUND,%d,%.9f,%llu\n", processes, result->seconds, result->attempts);
        return 2;
    }
    if(!des_ecb(c, result->key, cipher->data, cipher->len, 0)) return 1;
    int len = plaintext_length(cipher->data, cipher->len, o->padding);
    if(len < 0 || !contains(cipher->data, len, o->target)) return 1;
    if(!write_bytes(o->output, cipher->data, len)) {
        fprintf(stderr, "No se pudo guardar el texto recuperado: %s\n", o->output);
        return 1;
    }
    if(o->mode == MODE_DEMO) printf("Llave original: %llu\n", o->key);
    printf("Llave encontrada: %llu\nFrase objetivo verificada: %s\n", result->key, o->target);
    printf("Texto recuperado guardado: %s\n\nTexto recuperado:\n", o->output);
    fwrite(cipher->data, 1, (size_t)len, stdout);
    /* Legacy benchmark expects the input key as the first RESULT field. */
    printf("\nRESULT,%llu,%d,%.9f,%llu\n",
           o->mode == MODE_DEMO ? o->key : result->key,
           processes, result->seconds, result->attempts);
    return 0;
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    int rank, processes, status = 0;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &processes);
    Options options;
    int parsed = parse_options(argc, argv, &options);
    if(parsed != 1) {
        if(rank == 0) {
            if(parsed == 0) fputs("Argumentos invalidos.\n", stderr);
            usage();
        }
        MPI_Finalize();
        return parsed == 2 ? 0 : 1;
    }
    Crypto crypto = {0};
    if(!crypto_init(&crypto)) {
        fprintf(stderr, "Proceso %d: no se pudo inicializar DES-ECB/OpenSSL 3.\n", rank);
        MPI_Abort(comm, 1);
    }
    Buffer cipher = {0};
    if(rank == 0) {
        int ok = options.mode == MODE_CRACK ? load_cipher(&options, &cipher) :
                                             prepare_cipher(&crypto, &options, &cipher);
        if(!ok) {
            fprintf(stderr, "Error de archivo, formato o padding: %s\n", options.input);
            status = 1;
        }
    }
    MPI_Bcast(&status, 1, MPI_INT, 0, comm);
    if(!status && options.mode != MODE_ENCRYPT) {
        MPI_Bcast(&cipher.len, 1, MPI_INT, 0, comm);
        if(rank != 0) {
            cipher.data = malloc((size_t)cipher.len + 8);
            if(!cipher.data) MPI_Abort(comm, 1);
        }
        MPI_Bcast(cipher.data, cipher.len, MPI_BYTE, 0, comm);
        SearchResult result = search_mpi(&crypto, &options, &cipher, rank, processes, comm);
        if(rank == 0) status = report_result(&crypto, &options, &cipher, &result, processes);
        MPI_Bcast(&status, 1, MPI_INT, 0, comm);
    }
    free(cipher.data);
    crypto_free(&crypto);
    MPI_Finalize();
    return status;
}
