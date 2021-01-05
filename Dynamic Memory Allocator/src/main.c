#include <stdio.h>
#include "sfmm.h"

/*
 * Define WEAK_MAGIC during compilation to use MAGIC of 0x0 for debugging purposes.
 * Note that this feature will be disabled during grading.
 */
#ifdef WEAK_MAGIC
int sf_weak_magic = 1;
#endif

int main(int argc, char const *argv[]) {
   	char* ptr1 = sf_malloc(248);
   	sf_free(ptr1);


    return EXIT_SUCCESS;
}
