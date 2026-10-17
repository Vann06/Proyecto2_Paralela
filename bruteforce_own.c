#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <mpi.h>

#include <openssl/evp.h>
#include <openssl/provider.h>

EVP_CIPHER *des = NULL;
EVP_CIPHER_CTX *ctx = NULL;


const char search[] =
    "pan de banano rico y saludable";

// Pone el bit de paridad impar en cada byte de la llave (reemplaza des_setparity)
void set_odd_parity(unsigned char *k)
{
    for(int i = 0; i < 8; ++i)
    {
        /*
         * Dejamos libre el bit menos significativo.
         */
        unsigned char b = k[i] & 0xFE;

        /*
         * Contamos cuántos bits están en 1.
         */
        int ones = __builtin_popcount((unsigned int)b);
        k[i] = b | ((ones % 2) == 0);
    }
}

// Cifra o descifra len bytes (multiplo de 8) en sitio con DES-ECB
void build_des_key(
    unsigned long long key,
    unsigned char des_key[8]
)
{
    unsigned long long k = 0;

    /*
     * Este proceso conserva la lógica del
     * código original del proyecto.
     */
    for(int i = 0; i < 8; ++i)
    {
        key <<= 1;

        k |= (
            key &
            (0xFEULL << (i * 8))
        );
    }

    /*
     * Copiamos los 8 bytes de k.
     */
    memcpy(
        des_key,
        &k,
        8
    );

    /*
     * Ajustamos los bits de paridad.
     */
    set_odd_parity(des_key);
}

// Cifra o descifra len bytes (multiplo de 8) en sitio con DES-ECB
int des_ecb(
    unsigned long long key,
    unsigned char *data,
    int len,
    int enc
)
{
    unsigned char des_key[8];

    build_des_key(
        key,
        des_key
    );


    /*
     * Inicializamos DES-ECB.
     */
    if(
        EVP_CipherInit_ex2(
            ctx,
            des,
            des_key,
            NULL,
            enc,
            NULL
        ) != 1
    )
    {
        return 0;
    }


    /*
     * Nuestro texto ya estará completado
     * a múltiplos de 8.
     *
     * Por eso desactivamos padding de OpenSSL.
     */
    EVP_CIPHER_CTX_set_padding(
        ctx,
        0
    );


    int out_len = 0;
    int final_len = 0;


    /*
     * Procesamos TODOS los bytes del mensaje.
     *
     * Esta es una corrección importante respecto
     * al código original que enviaba 16.
     */
    if(
        EVP_CipherUpdate(
            ctx,
            data,
            &out_len,
            data,
            len
        ) != 1
    )
    {
        return 0;
    }


    /*
     * Finalizamos la operación.
     */
    if(
        EVP_CipherFinal_ex(
            ctx,
            data + out_len,
            &final_len
        ) != 1
    )
    {
        return 0;
    }


    return 1;
}

int encrypt(
    unsigned long long key,
    unsigned char *ciph,
    int len
)
{
    return des_ecb(
        key,
        ciph,
        len,
        1
    );
}

int decrypt(
    unsigned long long key,
    unsigned char *ciph,
    int len
)
{
    return des_ecb(
        key,
        ciph,
        len,
        0
    );
}

int tryKey(
    unsigned long long key,
    const unsigned char *ciph,
    int len
)
{
    /*
     * Buffer temporal.
     *
     * +1 para colocar '\0'.
     */
    unsigned char temp[len + 1];

    memcpy(
        temp,
        ciph,
        len
    );

    temp[len] = '\0';

    if(
        !decrypt(
            key,
            temp,
            len
        )
    )
    {
        return 0;
    }


    /*
     * Nos aseguramos otra vez de que
     * exista final de cadena.
     */
    temp[len] = '\0';


    /*
     * Buscamos nuestra frase conocida.
     */
    return strstr(
        (char *)temp,
        search
    ) != NULL;
}

char *read_text_file(
    const char *filename,
    long *file_size
)
{
    FILE *file =
        fopen(
            filename,
            "rb"
        );


    if(file == NULL)
    {
        return NULL;
    }


    /*
     * Vamos al final.
     */
    fseek(
        file,
        0,
        SEEK_END
    );

    long size =
        ftell(file);

    rewind(file);


    /*
     * Reservamos memoria.
     */
    char *text =
        malloc(
            size + 1
        );


    if(text == NULL)
    {
        fclose(file);
        return NULL;
    }


    /*
     * Leemos el archivo completo.
     */
    size_t bytes_read =
        fread(
            text,
            1,
            size,
            file
        );


    fclose(file);


    text[bytes_read] = '\0';

    *file_size =
        (long)bytes_read;


    return text;
}

int main(
    int argc,
    char *argv[]
)
{
    MPI_Init(
        &argc,
        &argv
    );


    MPI_Comm comm =
        MPI_COMM_WORLD;


    int N;
    int id;


    MPI_Comm_size(
        comm,
        &N
    );


    MPI_Comm_rank(
        comm,
        &id
    );

    unsigned long long the_key =
        50000ULL;


    unsigned long long upper =
        1000000ULL;


    const char *filename =
        "texto.txt";


    if(argc >= 2)
    {
        the_key =
            strtoull(
                argv[1],
                NULL,
                10
            );
    }


    if(argc >= 3)
    {
        upper =
            strtoull(
                argv[2],
                NULL,
                10
            );
    }


    if(argc >= 4)
    {
        filename =
            argv[3];
    }


    /*
     * Verificamos que la llave esté
     * dentro del espacio que buscaremos.
     */
    if(
        upper == 0 ||
        the_key >= upper
    )
    {
        if(id == 0)
        {
            fprintf(
                stderr,
                "ERROR: la llave debe ser menor que upper.\n"
            );

            fprintf(
                stderr,
                "Llave = %llu\n",
                the_key
            );

            fprintf(
                stderr,
                "Upper = %llu\n",
                upper
            );
        }

        MPI_Finalize();

        return 1;
    }
    const unsigned long long DES_KEY_SPACE =
        (1ULL << 56);


    if(upper > DES_KEY_SPACE)
    {
        if(id == 0)
        {
            fprintf(
                stderr,
                "ERROR: upper supera el espacio DES de 2^56.\n"
            );
        }

        MPI_Finalize();

        return 1;
    }

    OSSL_PROVIDER *legacy_provider =
        OSSL_PROVIDER_load(
            NULL,
            "legacy"
        );


    OSSL_PROVIDER *default_provider =
        OSSL_PROVIDER_load(
            NULL,
            "default"
        );


    des =
        EVP_CIPHER_fetch(
            NULL,
            "DES-ECB",
            NULL
        );


    ctx =
        EVP_CIPHER_CTX_new();


    if(
        legacy_provider == NULL ||
        default_provider == NULL ||
        des == NULL ||
        ctx == NULL
    )
    {
        fprintf(
            stderr,
            "Proceso %d: no se pudo cargar DES-ECB.\n",
            id
        );

        MPI_Abort(
            comm,
            1
        );
    }
    unsigned char *cipher =
        NULL;


    int ciphlen =
        0;


    long original_len =
        0;


    if(id == 0)
    {
        char *text =
            read_text_file(
                filename,
                &original_len
            );


        if(text == NULL)
        {
            fprintf(
                stderr,
                "No se pudo abrir %s\n",
                filename
            );

            MPI_Abort(
                comm,
                1
            );
        }


        /*
         * DES utiliza bloques de 8 bytes.
         *
         * Redondeamos hacia arriba.
         */
        ciphlen =
            (int)(
                (original_len + 7)
                & ~7L
            );


        cipher =
            malloc(
                ciphlen + 1
            );


        if(cipher == NULL)
        {
            fprintf(
                stderr,
                "Error reservando memoria.\n"
            );

            free(text);

            MPI_Abort(
                comm,
                1
            );
        }


        /*
         * Inicializamos todo con ceros.
         *
         * Los ceros sobrantes funcionan
         * como padding.
         */
        memset(
            cipher,
            0,
            ciphlen + 1
        );


        /*
         * Copiamos únicamente el texto real.
         */
        memcpy(
            cipher,
            text,
            original_len
        );

        if(
            !encrypt(
                the_key,
                cipher,
                ciphlen
            )
        )
        {
            fprintf(
                stderr,
                "Error al cifrar el texto.\n"
            );

            free(text);
            free(cipher);

            MPI_Abort(
                comm,
                1
            );
        }


        printf(
            "Texto cifrado correctamente.\n"
        );

        free(text);
    }


    /*
     * --------------------------------------------------------
     * ENVIAR LONGITUD A TODOS LOS PROCESOS
     * --------------------------------------------------------
     */

    MPI_Bcast(
        &ciphlen,
        1,
        MPI_INT,
        0,
        comm
    );


    /*
     * Los demás procesos crean
     * su propio buffer.
     */
    if(id != 0)
    {
        cipher =
            malloc(
                ciphlen + 1
            );


        if(cipher == NULL)
        {
            MPI_Abort(
                comm,
                1
            );
        }
    }

    MPI_Bcast(
        cipher,
        ciphlen,
        MPI_BYTE,
        0,
        comm
    );


    cipher[ciphlen] = '\0';

    unsigned long long range_per_node =
        upper /
        (unsigned long long)N;


    unsigned long long mylower =
        range_per_node *
        (unsigned long long)id;


    unsigned long long myupper;


    if(id == N - 1)
    {
        myupper =
            upper;
    }
    else
    {
        myupper =
            range_per_node *
            (unsigned long long)(id + 1);
    }


    printf(
        "Proceso %d -> rango [%llu, %llu)\n",
        id,
        mylower,
        myupper
    );

    unsigned long long stop_key =
        ULLONG_MAX;


    MPI_Request request;


    MPI_Irecv(
        &stop_key,
        1,
        MPI_UNSIGNED_LONG_LONG,
        MPI_ANY_SOURCE,
        0,
        comm,
        &request
    );


    /*
     * Cada proceso inicia suponiendo que
     * todavía no encontró nada.
     */
    unsigned long long local_found =
        ULLONG_MAX;


    unsigned long long attempts =
        0;


    int stop_received =
        0;

    MPI_Barrier(comm);

    double start_time =
        MPI_Wtime();


    /*
     * --------------------------------------------------------
     * FUERZA BRUTA
     * --------------------------------------------------------
     */

    for(
        unsigned long long i = mylower;
        i < myupper;
        ++i
    )
    {
        /*
         * Revisamos si otro proceso
         * encontró la llave.
         */
        MPI_Test(
            &request,
            &stop_received,
            MPI_STATUS_IGNORE
        );


        if(stop_received)
        {
            break;
        }


        attempts++;


        /*
         * Probamos la llave candidata.
         */
        if(
            tryKey(
                i,
                cipher,
                ciphlen
            )
        )
        {
            local_found =
                i;


            printf(
                "Proceso %d encontro la llave %llu\n",
                id,
                local_found
            );


            /*
             * Avisamos a los demás procesos
             * que pueden detenerse.
             */
            for(
                int node = 0;
                node < N;
                ++node
            )
            {
                if(node != id)
                {
                    MPI_Send(
                        &local_found,
                        1,
                        MPI_UNSIGNED_LONG_LONG,
                        node,
                        0,
                        comm
                    );
                }
            }


            break;
        }
    }


    double local_time =
        MPI_Wtime()
        -
        start_time;

    unsigned long long found;


    MPI_Allreduce(
        &local_found,
        &found,
        1,
        MPI_UNSIGNED_LONG_LONG,
        MPI_MIN,
        comm
    );

    if(request != MPI_REQUEST_NULL)
    {
        MPI_Cancel(
            &request
        );

        MPI_Wait(
            &request,
            MPI_STATUS_IGNORE
        );
    }

    double total_time =
        0.0;


    MPI_Reduce(
        &local_time,
        &total_time,
        1,
        MPI_DOUBLE,
        MPI_MAX,
        0,
        comm
    );


    unsigned long long total_attempts =
        0;


    MPI_Reduce(
        &attempts,
        &total_attempts,
        1,
        MPI_UNSIGNED_LONG_LONG,
        MPI_SUM,
        0,
        comm
    );

    if(id == 0)
    {
        if(found == ULLONG_MAX)
        {
            printf(
                "\nNo se encontro la llave.\n"
            );
        }
        else
        {
            /*
             * Desciframos el ciphertext
             * con la llave encontrada.
             */
            decrypt(
                found,
                cipher,
                ciphlen
            );


            cipher[ciphlen] =
                '\0';


            printf(
                "\n========================================\n"
            );

            printf(
                "RESULTADO\n"
            );

            printf(
                "========================================\n"
            );

            printf(
                "Llave original:      %llu\n",
                the_key
            );

            printf(
                "Llave encontrada:    %llu\n",
                found
            );

            printf(
                "Procesos MPI:         %d\n",
                N
            );

            printf(
                "Intentos totales:     %llu\n",
                total_attempts
            );

            printf(
                "Tiempo busqueda:      %.9f segundos\n",
                total_time
            );

            printf(
                "========================================\n"
            );


            printf(
                "\nTexto recuperado:\n\n%s\n",
                cipher
            );


            /*
             * Esta línea será utilizada
             * por benchmark.sh.
             */
            printf(
                "\nRESULT,%llu,%d,%.9f,%llu\n",
                the_key,
                N,
                total_time,
                total_attempts
            );
        }
    }
    free(cipher);


    EVP_CIPHER_CTX_free(
        ctx
    );


    EVP_CIPHER_free(
        des
    );


    OSSL_PROVIDER_unload(
        legacy_provider
    );


    OSSL_PROVIDER_unload(
        default_provider
    );


    MPI_Finalize();


    return 0;
}