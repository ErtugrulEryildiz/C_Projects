#include <stdio.h>

#include "audio.h"
#include "debug.h"

/*
 *@brief Helper function that helps to write header to a file.
 */
int unit32_write_file(FILE *out, uint32_t header);

/*
 *@brief helper function that reads 4 byte from file.
 */
int unit32_read_file(FILE *in, int *header_field);

int audio_read_header(FILE *in, AUDIO_HEADER *hp) {
   	// Read first 24 bytes, or until the EOF
   	do {
		int read_char = 0;

		// read magic number, if not valid break.
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		if(read_char == 0x2e736e64) (*hp).magic_number = 0x2e736e64;
		else break;
		// read data offset attribute.
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		else (*hp).data_offset = read_char;
		// read datasize attribute.
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		else (*hp).data_size = read_char;
		// read encoding attribute.
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		else if(read_char == 3) (*hp).encoding = 3;
		else break;
		// read sample rate attribute
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		else if (read_char == 8000) (*hp).sample_rate = 0x1f40;
		else break;
		// read channels attribute.
		if(unit32_read_file(in, &read_char) != 0) return EOF;
		else if(read_char == 1) (*hp).channels = 0x01;
		else break;
		// If every attribute read successfully, return 0.
	    return 0;
	} while(1);
    // If do-while loop breaks before returning, return EOF, meaning either EOF or incorrect header format.
    return EOF;
}

int audio_write_header(FILE *out, AUDIO_HEADER *hp) {
    do {
    	if(unit32_write_file(out, (*hp).magic_number) != 0) break;
    	if(unit32_write_file(out, (*hp).data_offset) != 0) break;
    	if(unit32_write_file(out, (*hp).data_size) != 0) break;
    	if(unit32_write_file(out, (*hp).encoding) != 0) break;
    	if(unit32_write_file(out, (*hp).sample_rate) != 0) break;
    	if(unit32_write_file(out, (*hp).channels) != 0) break;
    	return 0;
    } while(1);
    return EOF;
}

int audio_read_sample(FILE *in, int16_t *samplep) {
    if(in != NULL){
	    int read_byte;
	    int16_t bytes_2_return = 0;
	    read_byte = fgetc(in);
	    if(read_byte == -1) return EOF;
	    bytes_2_return += read_byte;
	    bytes_2_return = bytes_2_return << 8;
	    read_byte = fgetc(in);
	    if(read_byte == -1) return EOF;
 	   	bytes_2_return += read_byte;
 	   	*samplep = bytes_2_return;
 	   	return 0;
	} else {
    	return EOF;
    }
}

int audio_write_sample(FILE *out, int16_t sample) {
	if(out != NULL) {
	    unsigned char b1, b2;
	    b2 = sample & 0xff;
	    sample = sample >> 8;
	    b1 = sample & 0xff;
	    fputc(b1, out);
	    fputc(b2, out);
	    return 0;
	} else {
    return EOF;
	}
}

int unit32_write_file(FILE *out, uint32_t header) {
	if(out != NULL) {
		unsigned char b1, b2, b3, b4;

	    b4 = header & 0xff;
	    header = header >> 8;
	    b3 = header & 0xff;
	    header = header >> 8;
	    b2 = header & 0xff;
	    header = header >> 8;
	    b1 = header & 0xff;
	    header = header >> 8;

	    fputc(b1, out);
		fputc(b2, out);
	    fputc(b3, out);
	    fputc(b4, out);

	    return 0;
	} else {
		return EOF;
	}
}

int unit32_read_file(FILE *in, int *header_field) {
	if(in != NULL) {
		int read_char = 0;
		int index = 0;
		do {
			read_char = read_char << 8;
			read_char += fgetc(in);
			index++;
		} while(index < 4);
		*header_field = read_char;
		return 0;
	}
	else {
		return EOF;
	}
}