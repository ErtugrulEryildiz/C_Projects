#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "const.h"
#include "audio.h"
#include "dtmf.h"
#include "dtmf_static.h"
#include "goertzel.h"
#include "debug.h"

#ifdef _STRING_H
#error "Do not #include <string.h>. You will get a ZERO."
#endif

#ifdef _STRINGS_H
#error "Do not #include <strings.h>. You will get a ZERO."
#endif

#ifdef _CTYPE_H
#error "Do not #include <ctype.h>. You will get a ZERO."
#endif

/*
 * @brief Helper Function Prototype.
 * Compares 2 strings. returns 1 if they are equal.
 */
int str_comp(char *str1, char *str2);

/*
 * @brief Helper Function Prototype.
 * Converts string to integer. Return -111 if string value cannot be converted into integer.
 */
int str_to_int(char *str);

/*
 * @brief Helper function to write 16bit values to specified .au file.
 */
void write_2_file(int index, char symbol, FILE *audio_out, FILE *noise);


/*
 * @brief Reads DTMF event line from stdin, and changes poiter values accordingly.
 */
int read_DTMF_event(FILE *in, int *lower_index, int *upper_index, char *symbol);

/*
 * @brief fill goertzel_state array for every freq.
 * @detail find out k value for each goertzel state, then by using goertzel_init,
 * write those values to goertzel state array.
 */
void init_goertzel_state();

/*
 * @brief update s0, s1, s2 values of goertzel state structs in goertzel_state array.
 */
void update_goertzel_steps(double audio_sample);

/*
 * @brief Find the strongest column frequency and row frequency by comparing their strengths.
 * @detail  Get the strength value for passed audio sample for each frequecny, later compare
 *          these valeus and see if we can find a symbol associated with any frequencies. If
 *		    so, return symbol, if not return '\0'.
 */
char find_strongest_freqs(double audio_sample);

/*
 * @brief helper function that writes block informations to dtmf events file.
 */
void write_DTMF_2_file(int lower_block_index, int upper_block_index, char symbol, FILE *events_out);

/*
 * You may modify this file and/or move the functions contained here
 * to other source files (except for main.c) as you wish.
 *
 * IMPORTANT: You MAY NOT use any array brackets (i.e. [ and ]) and
 * you MAY NOT declare any arrays or allocate any storage with malloc().
 * The purpose of this restriction is to force you to use pointers.
 * Variables to hold the pathname of the current file or directory
 * as well as other data have been pre-declared for you in const.h.
 * You must use those variables, rather than declaring your own.
 * IF YOU VIOLATE THIS RESTRICTION, YOU WILL GET A ZERO!
 */

/**
 * DTMF generation main function.
 * DTMF events are read (in textual tab-separated format) from the specified
 * input stream and audio data of a specified duration is written to the specified
 * output stream.  The DTMF events must be non-overlapping, in increasing order of
 * start index, and must lie completely within the specified duration.
 * The sample produced at a particular index will either be zero, if the index
 * does not lie between the start and end index of one of the DTMF events, or else
 * it will be a synthesized sample of the DTMF tone corresponding to the event in
 * which the index lies.
 *
 *  @param events_in  Stream from which to read DTMF events.
 *  @param audio_out  Stream to which to write audio header and sample data.
 *  @param length  Number of audio samples to be written.
 *  @return 0 if the header and specified number of samples are written successfully,
 *  EOF otherwise.
 */
int dtmf_generate(FILE *events_in, FILE *audio_out, uint32_t length) {
	int lower_sample;	// Audio sample's lower index
	int upper_sample;	// Audio sample's upper index
	char symbol;		// Audio sample's  symbol
	int no_DTMF_Event = 0;	// Flag to tell if program hit the endo of DTMF file
	int sample_index = 0;	// Auido sample index to keep count of how many samples are generetaded.
    FILE *noise;			// Noise file

	// Generate audio_out header with default values
	struct audio_header au;
    au.magic_number = 0x2e736e64;
    au.data_offset = 24;
    au.data_size = length*2;
    au.encoding = 3;
    au.sample_rate = 8000;
    au.channels = 1;
    audio_write_header(audio_out, &au);

 	struct audio_header noise_header;
    if(noise_file != NULL) { // If noise file exist
    	noise = fopen(noise_file, "r");
    	audio_read_header(noise, &noise_header);
    	// Increment file pointer to pass entire header and get to audio samples.
    	for(int index = 0; index < noise_header.data_offset - 24; index++) fgetc(noise);
    }

	// read line, populate block's lower index, upper index, and symbol. If unsuccessful, return -1.
	if(read_DTMF_event(events_in, &lower_sample, &upper_sample, &symbol) != 0) return -1;
	// for each sample index, write proper bytes to file.
	while(sample_index < length) {
		// If our index less than DTMF Event lower index range, write 0.
		if(sample_index < lower_sample) {
			write_2_file(sample_index, '\0', audio_out, noise);
		}
		// If our sample index in between DTMF Event indexes, write symbol.
		else if(lower_sample <= sample_index && sample_index < upper_sample) {
			write_2_file(sample_index, symbol, audio_out, noise);
		}
		// If sample index greater than DTMF Event's upper index range, and there are still DTMF events to read,
		// then read next line from DTMF events.
		else if(sample_index >= upper_sample && no_DTMF_Event == 0) {
			// If no DTMF Events left, type 0's
			if(read_DTMF_event(events_in, &lower_sample, &upper_sample, &symbol) != 0) no_DTMF_Event = 1;
			write_2_file(sample_index, '\0', audio_out, noise);
		}
		// If sample index greater than DTMF Event index range, and there aren't any DTMF event left
		// to read, write 0.
		else if(sample_index >= upper_sample && no_DTMF_Event == 1) {
			write_2_file(sample_index, '\0', audio_out, noise);
		}
		sample_index++;
	}
	// Successfull exit, if noise was openned, close it.
	if(noise_file != NULL) fclose(noise);
    return 0;
}

/**
 * DTMF detection main function.
 * This function first reads and validates an audio header from the specified input stream.
 * The value in the data size field of the header is ignored, as is any annotation data that
 * might occur after the header.
 *
 * This function then reads audio sample data from the input stream, partititions the audio samples
 * into successive blocks of block_size samples, and for each block determines whether or not
 * a DTMF tone is present in that block.  When a DTMF tone is detected in a block, the starting index
 * of that block is recorded as the beginning of a "DTMF event".  As long as the same DTMF tone is
 * present in subsequent blocks, the duration of the current DTMF event is extended.  As soon as a
 * block is encountered in which the same DTMF tone is not present, either because no DTMF tone is
 * present in that block or a different tone is present, then the starting index of that block
 * is recorded as the ending index of the current DTMF event.  If the duration of the now-completed
 * DTMF event is greater than or equal to MIN_DTMF_DURATION, then a line of text representing
 * this DTMF event in tab-separated format is emitted to the output stream. If the duration of the
 * DTMF event is less that MIN_DTMF_DURATION, then the event is discarded and nothing is emitted
 * to the output stream.  When the end of audio input is reached, then the total number of samples
 * read is used as the ending index of any current DTMF event and this final event is emitted
 * if its length is at least MIN_DTMF_DURATION.
 *
 *   @param audio_in  Input stream from which to read audio header and sample data.
 *   @param events_out  Output stream to which DTMF events are to be written.
 *   @return 0  If reading of audio and writing of DTMF events is sucessful, EOF otherwise.
 */
int dtmf_detect(FILE *audio_in, FILE *events_out) {
	int lower_block_index = 0;		// lower block index
	char prev_symbol = '$',			// Initial dummy symbol for previous symbol
		 current_symbol;
	int current_sample_index = 0;
	int16_t read_sample;			// Variable to hold current read audio sample

   	// Read header
	struct audio_header au_header;

	if(audio_read_header(audio_in, &au_header) != 0) return -1;
	// Increment file pointer to the end of data offset.
	for(int i=0; i<(au_header.data_offset-24); i++) fgetc(audio_in);

	while(1) {
	   	// Initialize goertzel states for each frequency.
		init_goertzel_state();

		// Main loop of goertzel algorithm. Read samples excluding last sample from block.
		for(int index=0; index<block_size-1; index++) {
			// If we come to end of file while reading current block, either because block is not full size or it doesn't exist at all,
			//  the we should write previous block to DTMF event file before exiting dtmf_detect().
			if(audio_read_sample(audio_in, &read_sample) != 0) {
				if((current_sample_index - block_size)/(double)AUDIO_FRAME_RATE >= MIN_DTMF_DURATION) {
					write_DTMF_2_file(lower_block_index,current_sample_index, prev_symbol, events_out);
				}
				return 0;
			}
			current_sample_index++;
			update_goertzel_steps((double)read_sample/INT16_MAX);
		}

		// Read the (N-1)th audio sample of this block and find strongest frequencies.
		if(audio_read_sample(audio_in, &read_sample) != 0) {
			if((current_sample_index - block_size)/(double)AUDIO_FRAME_RATE >= MIN_DTMF_DURATION) {
					write_DTMF_2_file(lower_block_index,current_sample_index, prev_symbol, events_out);
				}
			return 0;
		}
		current_sample_index++;
		current_symbol = find_strongest_freqs((double)read_sample/INT16_MAX);

		// If we read first block, set previous symbol to current symbol.
		if(prev_symbol == '$') {
			prev_symbol = current_symbol;
		}
		else
		{
			// If our previous symbol is not same as current symbol, we need to write prev. to file.
			if(prev_symbol != current_symbol) {
				// If our previous symbol is null(corrupted block), we shouldn't write it to file.
				if(prev_symbol == '\0') {
					lower_block_index = current_sample_index-block_size;
					prev_symbol = current_symbol;
				}
				// If our previous symbol is acceptable, write it to file.
				else {
					if((current_sample_index - (block_size + lower_block_index))/(double)AUDIO_FRAME_RATE >= MIN_DTMF_DURATION) {
						write_DTMF_2_file(lower_block_index,current_sample_index-block_size, prev_symbol, events_out);
					}
					lower_block_index = current_sample_index-block_size;
					prev_symbol = current_symbol;
				}
			}
		}
	}
    return 0;
}

/**
 * @brief Validates command line arguments passed to the program.
 * @details This function will validate all the arguments passed to the
 * program, returning 0 if validation succeeds and -1 if validation fails.
 * Upon successful return, the operation mode of the program (help, generate,
 * or detect) will be recorded in the global variable `global_options`,
 * where it will be accessible elsewhere in the program.
 * Global variables `audio_samples`, `noise file`, `noise_level`, and `block_size`
 * will also be set, either to values derived from specified `-t`, `-n`, `-l` and `-b`
 * options, or else to their default values.
 *
 * @param argc The number of arguments passed to the program from the CLI.
 * @param argv The argument strings passed to the program from the CLI.
 * @return 0 if validation succeeds and -1 if validation fails.
 * @modifies global variable "global_options" to contain a bitmap representing
 * the selected program operation mode, and global variables `audio_samples`,
 * `noise file`, `noise_level`, and `block_size` to contain values derived from
 * other option settings.
 */
int validargs(int argc, char **argv){
    // Before we execute the rest of the code, make sure there are flags and arguments.
    if(argc == 1) return -1;

    char **command_line_args;
	command_line_args = argv;
	command_line_args++; // Skip first element, bin/dtmf.

	// Char pointer for each flag in the command line
	char *flag;
	flag = *command_line_args;

	// Assigning default constant values for global values. These values may change.
	audio_samples = 1000*8;   // Number of samples in generated audio file.
	noise_level = 0;     // Ratio (in dB) of noise level to DTMF tone level.
	block_size = 100;      // Block size used in DTMF tone detection.
	noise_file = NULL;    // Name of noise file, or NULL if none.

	// HELP flag
	if (str_comp(flag, "-h") == 1) {
		global_options = 0x1;
		return EXIT_SUCCESS;
	}
	// GENERATE flag
	else if(str_comp(flag, "-g") == 1) {
		// if we have more than 8 arugments or odd number of arguments, exit-failure.
		if(argc > 8 || argc % 2 != 0) return -1;
		// Look through each possible optional argument
		for(int index = 0; index < (argc-2)/2; index++) {
			command_line_args++;
			flag = *command_line_args;

			if(str_comp(flag, "-t") == 1) {
				command_line_args++;
				flag = *command_line_args;
				int msec = str_to_int(flag);

				if (msec >= 0 && msec <= UINT32_MAX) {
					audio_samples = msec*8;
					global_options = 0x2;
				}
				// If msec value is not valid, dont check following flags, return exit failure
				else {
					return -1;
				}
			}
			else if(str_comp(flag, "-n") == 1) {
				command_line_args++;
				flag = *command_line_args;
				char *noise_url = flag;
				noise_file = noise_url;
				global_options = 0x2;
			}
			else if(str_comp(flag, "-l") == 1) {
				command_line_args++;
				flag = *command_line_args;
				int level_value = str_to_int(flag);

				if (level_value >= -30 && level_value <= 30) {
					noise_level = level_value;
					global_options = 0x2;
				}
				// If msec value is not valid, dont check following flags, return exit failure
				else {
					return -1;
				}
			}
			else { // Invalid optional flag.
				return -1;
			}
		}
		// End of for loop
		return EXIT_SUCCESS;
	}
	// DETECT flag
	else if(str_comp(flag, "-d") == 1) {
		// If we have exessive amount of flags or fewer than expected, exit-failure.
		if(argc > 4 || argc % 2 != 0) { return -1; }
		// if we have no additional flag after d.
		if(argc == 2) {
			return EXIT_SUCCESS;
			global_options = 0x4;
		}

		command_line_args++;
		flag = *command_line_args;

		if(str_comp(flag, "-b") == 1) {
			command_line_args++;
			flag = *command_line_args;
			int b_size = str_to_int(flag);
			if(b_size >= 10 && b_size <= 1000) {
				block_size = b_size;
				global_options = 0x4;
				return EXIT_SUCCESS;
			}
		}
		else { // Invalid Optional flag
			return -1;
		}
	}
	// Incorrect Positional Argument
	else {
		return -1;
	}
	// Just incase there appears to be any form of failure.
    return -1;
}


int str_comp(char *str1, char *str2) {
	int len1 = 0;
	int len2 = 0;
	char *string1 = str1;
	char *string2 = str2;

	while (*string1 != '\0') {
		len1++;
		string1++;
	}

	while (*string2 != '\0') {
		len2++;
		string2++;
	}

	if(len1 != len2) return 0;

	int index;
	string1 = str1;
	string2 = str2;

	for(index = 0; index <= len1; index++) {
		if (*string1 != *string2)
			return 0;
		string1++;
		string2++;
	}

	return 1;
}

int str_to_int(char *str) {
	int sign = 1;
	int num = 0;
	int read_char;
	char *current_char = str;

	read_char = *current_char;
	if(read_char == 45) {
		sign = -1;
		current_char++;
		read_char = *current_char;
	}

	while(read_char != '\0') {
		if(read_char >= 48 && read_char <= 57) {
			num *= 10;
			num += read_char-48;
			current_char++;
			read_char = *current_char;
		} else {
			return -111;
		}
	}

	return sign*num;
}

void write_2_file(int index, char symbol, FILE *audio_out, FILE *noise) {
	// If our index value isn't in between min-max range, write 0. Otherwise, do required calculations.
	if(symbol == '\0') {
		// IF no noise file exist write 0000 to file.
		if(noise_file == NULL) {
			fputc(0x00, audio_out);
			fputc(0x00, audio_out);
		}
		// IF noise file exist when we need to write 0000, then combine that audio sample with noise file.
		else {
			int16_t noise_val = 0;
			int16_t return_val = 0;
			if(audio_read_sample(noise, &noise_val) != 0) {
				printf("%s\n", "error reading noise");
				return;
			}

			double w = pow(10, (noise_level/10)) / (pow(10, (noise_level/10)) +1);
			return_val = (int16_t) ((noise_val)*w + (0x0000)*(1-w));
			if(audio_write_sample(audio_out, return_val) != 0) {
				printf("Error writing to file.\n");
			}
		}
	}
	else {
		int i = 0;
		int row_index;
		int column_index;
		int f_c;
		int f_r;
		int16_t sample_val;
		int16_t noise_val = 0;
		int16_t return_val;

		// Pointer to dtmf symbols
		uint8_t *ptr = *dtmf_symbol_names;

		// Find column and row values of symbol from dtmf_symbol_names 2D array.
		while (*ptr != symbol) {
			ptr++;
			i++;
		}
		column_index = i % 4;
		row_index = i / 4;
		f_c = *(dtmf_freqs + row_index);
		f_r =  *(dtmf_freqs + column_index + 4);
		sample_val = (int16_t) ( 0.5 * ( (cos(2.0*M_PI*f_r*index/AUDIO_FRAME_RATE) + cos(2.0*M_PI*f_c*index/AUDIO_FRAME_RATE) ) * INT16_MAX) );

		// IF noise file exist, calculate w value and write accordingly.
		if(noise_file != NULL) {
			if(audio_read_sample(noise, &noise_val) != 0) {
				printf("%s\n", "error reading noise");
				return;
			}

			double w = pow(10, (noise_level/10)) / (pow(10, (noise_level/10)) +1);
			return_val = (int16_t) (noise_val)*w + sample_val*(1-w);
			if(audio_write_sample(audio_out, return_val) != 0) {
				printf("Error writing to file.\n");
			}
		}
		// If no noise exist.
		else{
			return_val = sample_val;
			if(audio_write_sample(audio_out, return_val) != 0) {
				printf("Error writing to file.\n");
			}
		}
	}
}

int read_DTMF_event(FILE *in, int *lower_index, int *upper_index, char *symbol) {
	int n1 = 0, n2 = 0;
	char *ptr, sym;
	ptr = line_buf;
	*ptr = fgetc(in);
	// if there isn't a line to read, return EOF
	if(*ptr == -1) return EOF;
	// Read line to buffer
	do{
		ptr++;
		*ptr = fgetc(in);
	}while (*ptr != '\n');
	// get the lower index
	ptr = line_buf;
	do {
		n1 *= 10;
		n1 += (*ptr - 48);
		ptr++;
	} while (*ptr != '\t');
	ptr++;
	// get the upper index
	do {
		n2 *= 10;
		n2 += (*ptr - 48);
		ptr++;
	} while (*ptr != '\t');
	// get the symbol
	ptr++;
	sym = *ptr;

	*lower_index = n1;
	*upper_index = n2;
	*symbol = sym;

	return 0;
}

void init_goertzel_state() {
	struct goertzel_state gp;
	int *freq_ptr = dtmf_freqs;
	GOERTZEL_STATE *gp_arr_ptr = goertzel_state;

	for (int i=0; i<8; i++) {
		double k = (double)(block_size * (*freq_ptr)) / AUDIO_FRAME_RATE;
		goertzel_init(&gp, block_size, k);
		*gp_arr_ptr = gp;
		freq_ptr++;
		gp_arr_ptr++;
	}
}

void update_goertzel_steps(double audio_sample) {
	GOERTZEL_STATE *gp_arr_ptr = goertzel_state;
	for (int i=0; i<8; i++) {
		goertzel_step(gp_arr_ptr, audio_sample);
		gp_arr_ptr++;
	}
}

char find_strongest_freqs(double audio_sample) {
	GOERTZEL_STATE *gp_arr_ptr = goertzel_state;
	double r1 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double r2 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double r3 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double r4 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double c1 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double c2 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double c3 = goertzel_strength(gp_arr_ptr, audio_sample);
	gp_arr_ptr++;
	double c4 = goertzel_strength(gp_arr_ptr, audio_sample);

	// Compare rows and columns within themselves.
	double gr_1, gr_2, g_r, gc_1, gc_2, g_c;
	int left_row_index, right_row_index,
		left_column_index, right_column_index,
		greatest_row_index, greatest_column_index;


	if(r1 > r2) {
		gr_1 = r1;
		left_row_index = 1;
	} else {
		gr_1 = r2;
		left_row_index = 2;
	}
	if(r3 > r4) {
		gr_2 = r3;
		right_row_index = 3;
	} else {
		gr_2 = r4;
		right_row_index = 4;
	}
	if(gr_1 > gr_2) {
		greatest_row_index = left_row_index;
		g_r = gr_1;
	} else {
		g_r = gr_2;
		greatest_row_index = right_row_index;
	}

	if(c1 > c2) {
		gc_1 = c1;
		left_column_index = 1;
	} else {
		gc_1 = c2;
		left_column_index = 2;
	}
	if(c3 > c4) {
		gc_2 = c3;
		right_column_index = 3;
	} else {
		gc_2 = c4;
		right_column_index = 4;
	}
	if(gc_1 > gc_2) {
		g_c = gc_1;
		greatest_column_index = left_column_index;
	} else {
		g_c = gc_2;
		greatest_column_index = right_column_index;
	}

	if(g_r != r1 && g_r / r1 < SIX_DB) { return '\0'; }
	if(g_r != r2 && g_r / r2 < SIX_DB) { return '\0'; }
	if(g_r != r3 && g_r / r3 < SIX_DB) { return '\0'; }
	if(g_r != r4 && g_r / r4 < SIX_DB) { return '\0'; }
	if(g_c != c1 && g_c / c1 < SIX_DB) { return '\0'; }
	if(g_c != c2 && g_c / c2 < SIX_DB) { return '\0'; }
	if(g_c != c3 && g_c / c3 < SIX_DB) { return '\0'; }
	if(g_c != c4 && g_c / c4 < SIX_DB) { return '\0'; }
	// If greatest row to greatest column ratio is not in between [-4dB, 4dB] reject.
	if (((g_r / g_c) < 1/FOUR_DB ) || (g_r / g_c > FOUR_DB)) { return '\0'; }
	// If greatest row and column doesnt add up to atleast -20dB, reject.
	if (g_r + g_c < MINUS_20DB) { return '\0'; }

	return *(*(dtmf_symbol_names+greatest_row_index-1)+greatest_column_index-1);
}

void write_DTMF_2_file(int lower_block_index, int upper_block_index, char symbol, FILE *events_out) {
	char *ptr1 = line_buf;
	int lower_copy = lower_block_index,
	    upper_copy = upper_block_index,
	    len = 0;

	// min length can be 0 for first block.
    if (lower_block_index == 0) {
    	fputc('0', events_out);
    	fputc('\t', events_out);
    } else {
		while(lower_copy != 0) {
			len++;
			lower_copy /= 10;
		}
		for(int i=0; i<len; i++) {
			*ptr1 = lower_block_index%10;
			lower_block_index /= 10;
			ptr1++;
		}
		for(int i=0; i<len; i++) {
			ptr1--;
			fputc((*ptr1 + 48), events_out);
		}
		fputc('\t', events_out);
	}
	len = 0;
	ptr1 = line_buf;
	while(upper_copy != 0) {
		len++;
		upper_copy /= 10;
	}
	for(int i=0; i<len; i++) {
		*ptr1 = upper_block_index%10;
		upper_block_index /= 10;
		ptr1++;
	}
	for(int i=0; i<len; i++) {
		ptr1--;
		fputc((*ptr1 + 48), events_out);
	}
	fputc('\t', events_out);

	fputc(symbol, events_out);
	fputc('\n', events_out);
}