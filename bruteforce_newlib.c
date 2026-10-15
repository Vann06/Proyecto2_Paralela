#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/provider.h>

EVP_CIPHER *des = NULL;
EVP_CIPHER_CTX *ctx = NULL;


// Pone el bit de paridad impar en cada byte de la llave (reemplaza des_setparity)
void set_odd_parity(unsigned char *k){
  for(int i=0; i<8; ++i){
    unsigned char b = k[i] & 0xFE;
    int ones = __builtin_popcount(b);
    k[i] = b | ((ones % 2) == 0);
  }
}


// Cifra o descifra len bytes (multiplo de 8) en sitio con DES-ECB
void des_ecb(long key, char *ciph, int len, int enc){
  long k = 0;
  for(int i=0; i<8; ++i){
    key <<= 1;
    k += (key & (0xFEL << i*8));
  }
  set_odd_parity((unsigned char *)&k);
  int outl;
  EVP_CipherInit_ex2(ctx, des, (unsigned char *)&k, NULL, enc, NULL);
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  EVP_CipherUpdate(ctx, (unsigned char *)ciph, &outl, (unsigned char *)ciph, len);
}


void decrypt(long key, char *ciph, int len){
  des_ecb(key, ciph, len, 0);
}


void encrypt(long key, char *ciph, int len){
  des_ecb(key, ciph, len, 1);
}




char search[] = "los mismos del proyecto 1";
int tryKey(long key, char *ciph, int len){
  char temp[len+1];
  memcpy(temp, ciph, len);
  temp[len]=0;
  decrypt(key, temp, len);
  return strstr((char *)temp, search) != NULL;
}

char eltexto[] = " En equipos de 3 integrantes (los mismos del proyecto 1), deben utilizar MPI para mejorar un programa que encuentra la llave privada con la que fué cifrado un texto plano.  La búsqueda se hará probando todas las posibles combinaciones de llaves, hasta encontrar una que descifra el texto. ";
long the_key = 18014398509481984;


int main(int argc, char *argv[]){
  int N, id;
  long upper = (1L <<56); //upper bound DES keys 2^56
  long mylower, myupper;
  MPI_Status st;
  MPI_Request req;

  int ciphlen = (strlen(eltexto) + 7) & ~7; // DES cifra bloques de 8 bytes
  MPI_Comm comm = MPI_COMM_WORLD;

  MPI_Init(NULL, NULL);

  OSSL_PROVIDER_load(NULL, "legacy"); // DES vive en el provider legacy de OpenSSL 3
  OSSL_PROVIDER_load(NULL, "default");
  des = EVP_CIPHER_fetch(NULL, "DES-ECB", NULL);
  ctx = EVP_CIPHER_CTX_new();
  if(des == NULL || ctx == NULL){
    fprintf(stderr, "No se pudo cargar DES-ECB de OpenSSL\n");
    MPI_Abort(comm, 1);
  }

  char cipher[ciphlen+1];
  memset(cipher, 0, ciphlen+1);
  memcpy(cipher, eltexto, strlen(eltexto));
  encrypt(the_key, cipher, ciphlen);

  MPI_Comm_size(comm, &N);
  MPI_Comm_rank(comm, &id);

  long found = 0;
  int ready = 0;`

  long range_per_node = upper / N;
  mylower = range_per_node * id;
  myupper = range_per_node * (id+1) -1;
  if(id == N-1){
    myupper = upper;
  }
  printf("Process %d lower %ld upper %ld\n", id, mylower, myupper);

  MPI_Irecv(&found, 1, MPI_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

  for(long i = mylower; i<myupper; ++i){
    MPI_Test(&req, &ready, MPI_STATUS_IGNORE);
    if(ready)
      break;

    if(tryKey(i, cipher, ciphlen)){
      found = i;
      printf("Process %d found the key\n", id);
      for(int node=0; node<N; node++){
        MPI_Send(&found, 1, MPI_LONG, node, 0, comm);
      }
      break;
    }
  }

  if(id==0){
    MPI_Wait(&req, &st);
    decrypt(found, cipher, ciphlen);
    printf("Key = %li\n\n", found);
    printf("%s\n", cipher);
  }

  EVP_CIPHER_CTX_free(ctx);
  EVP_CIPHER_free(des);
  MPI_Finalize();
}
