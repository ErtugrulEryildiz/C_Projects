#include <stdio.h>
#include <stdlib.h>

#include "const.h"
#include "debug.h"
#include "goertzel.h"

#ifdef _STRING_H
#error "Do not #include <string.h>. You will get a ZERO."
#endif

#ifdef _STRINGS_H
#error "Do not #include <strings.h>. You will get a ZERO."
#endif

#ifdef _CTYPE_H
#error "Do not #include <ctype.h>. You will get a ZERO."
#endif

int main(int argc, char **argv)
{

    if(validargs(argc, argv))
        USAGE(*argv, EXIT_FAILURE);
    if(global_options & 1)
        USAGE(*argv, EXIT_SUCCESS);

    // Generate Audio File
    if(global_options == 0x02) {
    	return dtmf_generate(stdin, stdout, audio_samples);
    }
    // Detect Audio File
    if(global_options == 0x04) {
    	return dtmf_detect(stdin, stdout);
    }
    else
		return EXIT_FAILURE;
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */
