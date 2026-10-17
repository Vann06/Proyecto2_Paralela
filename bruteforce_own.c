#include <stdio>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT 10000


// recibe 7 bits de la llave y genera 1 byte de 8 bits con paridad impart 
unsigned char setOddParity(unsigned char sevenBits)
{
    unsigned char byte;
    int ones = 0; 

    byte = (sevenBits & 0x7F) << 1; // Desplaza los 7 bits a la izquierda y deja el bit de paridad en 0

    // contar cuantos bits de 1 existen 
    for (int i = 0; i < 7; i++)
    {
        if (sevenBits & (1 << i))
        {
            ones++;
        }
    }

    //DES paridad impar 
    if (ones % 2 == 0)
    {
        byte |= 1; // Si hay un número par de bits de 1, establece el bit de paridad en 1
    }
    return byte;
}


// convertir un numero de hasta 56 bits en una llave de 8 bytes 
void numberToDESKey(uint64_t number, unsigned char key[8])
{
    // conservar solo los 56 bits menos significativos
    number &= 0x00FFFFFFFFFFFFFF;

    //dividir los 56 bits en 8 bytes de 7 bits y agregar el bit de paridad impar
    for (int i = 0; i < 8; i++)
    {
        int shift = (7-i) *7;
        unsigned char sevenBits = (number >> shift) & 0x7F; // extrae 7 bits
        key[i] = setOddParity(sevenBits);                 // genera el byte con paridad impar
    }
}


int main()
{
    FILE *archivo;

    char texto[MAX_TEXT];

    archivo = fopen("texto.txt", "r");
    if (archivo == NULL)
    {
        printf("No se pudo abrir el archivo.\n");
        return 1;
    }
    size_t len = fread(
        texto, sizeof(char), MAX_TEXT - 1, archivo);
    fclose(archivo);

    texto[len] = '\0';
    printf("Contenido del archivo:\n%s\n", texto);
    printf("Longitud del texto: %zu\n", len);
    printf("Tamaño del texto: %zu bytes\n", sizeof(texto));
    return 0;

}



// crear la llave 
void createKey(long keyNumber, DES_cblock *key)
{
    for (int i = 0; i < 8; i++)
    {
        (*key)[i] = (keyNumber >> (56 - 8 * i)) & 0xFF;
    }
}