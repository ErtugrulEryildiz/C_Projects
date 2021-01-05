/****************************************************************\
|  finddup.c - the one true find duplicate files program
|----------------------------------------------------------------
|  Bill Davidsen, last hacked 2/22/91
|  Copyright (c) 1991 by Bill Davidsen, all rights reserved. This
|  program may be freely used and distributed in its original
|  form. Modified versions must be distributed with the original
|  unmodified source code, and redistribution of the original code
|  or any derivative program may not be restricted.
|----------------------------------------------------------------
|  Calling sequence:
|   finddup [-l] checklist
|
|  where checklist is the name of a file containing filenames to
|  be checked, such as produced by "find . -type f -print >file"
|  returns a list of linked and duplicated files.
|
|  If the -l option is used the hard links will not be displayed.
\***************************************************************/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <unistd.h>

/* constants */
#define EOS		((char) '\0')	/* end of string */
#define FL_CRC	0x0001			/* flag if CRC valid */
#define FL_DUP	0x0002			/* files are duplicates */
#define FL_LNK	0x0004			/* file is a link */

/* macros */
#ifdef DEBUG
	#define debug(X) if (DebugFlg) printf X
	#define OPTSTR	"lhd"
#else
	#define debug(X)
	#define OPTSTR	"lh"
#endif

#define SORT qsort(filelist, n_files, sizeof(filedesc), comp1);

#define GetFlag(x,f) ((filelist[x].flags & (f)) != 0)
#define SetFlag(x,f) (filelist[x].flags = (f))

typedef struct {
	off_t length;				/* file length */
	unsigned long crc32;		/* CRC for same length */
	dev_t device;				/* physical device # */
	ino_t inode;				/* inode for link detect */
	off_t nameloc;				/* name loc in names file */
	char flags;					/* flags for compare */
} filedesc;

static struct option command_ops[] = {	/*Command line argument structure. 6*/
	{"help", 0, 0, 'h'},
	{"no-links", 0, 0, 'l'},
	{"debug", 2, 0, 'd'}
};

char *line_buffer = NULL;		/* Line buffer pointer to dynamically store files names. 6*/
size_t line_size = 0;			/* Used to specify size of the allocated buffer 6*/
int char_count;					/* used to save the returned value from getline(3) method. 6*/

filedesc *filelist;				/* master sorted list of files */
long n_files = 0;				/* # files in the array */
long max_files = 0;				/* entries allocated in the array */
int linkflag = 1;				/* show links */
int DebugFlg = 0;				/* inline debug flag */
FILE *namefd;					/* file for names */

extern int
	opterr,
	optind;
	char *optarg;				/* pointer to a flag argument. 6*/

/* help message, in a table format */
static char *HelpMsg[] = {
	"Calling sequence:",
    "",
	"  finddup [options] list",
	"",
	"where list is a list of files to check, such as generated",
	"by \"find . -type f -print > file\"",
	"",
	"Options:",
	"  -l - don't list hard links",
#ifdef DEBUG
	"  -d - debug (must compile with DEBUG)"
#endif /* ?DEBUG */
};

static int HelpLen = sizeof(HelpMsg)/sizeof(char *);	/* Added int return type to prototype decleration. 6*/
static int fullcmp(int v1, int v2);						/* Function prototype created. 6*/

#ifndef	lint
	static char *SCCSid[] = {
		"@(#)finddup.c v1.13 - 2/22/91",
		"Copyright (c) 1991 by Bill Davidsen, all rights reserved"
	};
#endif

extern uint32_t rc_crc32(uint32_t crc, const char *buf, size_t len);	/* given crc 32 function by prof.*/
int comp1(const void *p1, const void *p2);	/* compare two filedesc's */
void scan1();					/* make the CRC scan */
void scan2();					/* do full compare if needed */
void scan3();					/* print the results */
uint32_t get_crc();				/* get crc32 on a file */
char *getfn();					/* get a filename by index */
int str_to_int(char *str);		/* converts string to int if possible. 6*/

int finddup_main(argc, argv)
int argc;
char *argv[];
{
	struct stat statbuf;
	int ch;
	int firsterr = 0;			/* flag on 1st error for format */
	int firsttrace = 0;			/* flag for 1st trace output */
	off_t loc;            		/* location of name in the file */
	int zl_hdr = 1;				/* need header for zero-length files list */
	filedesc *curptr;			/* pointer to current storage loc */

	/* parse options, if any */
	opterr = 0;
	// while ((ch = att_getopt(argc, argv, OPTSTR)) != EOF) { //
	while ((ch = getopt_long(argc, argv, OPTSTR, command_ops, 0)) != EOF) {
		switch (ch) {
		case 'l': /* set link flag */
			linkflag = 0;
			break;
#ifdef DEBUG
		case 'd': /* debug */
			if (optarg != NULL) { /* If --debug flag is passed*/
				int arg;
				if ((arg = str_to_int(optarg)) != -1)
					DebugFlg += arg;
			} else {
				++DebugFlg;
			}
			break;
#endif /* ?DEBUG */
		case 'h': /* help */
		case '?':
			for (ch = 0; ch < HelpLen; ++ch) {
				printf("%s\n", HelpMsg[ch]);
			}
			exit(1);
		}
	}

	/* correct for the options */
	argc -= (optind-1);
	argv += (optind-1);

	/* check for filename given, and open it */
	if (argc != 2) {
		fprintf(stderr, "Needs name of file with filenames\n");
		exit(1);
	}
	namefd = fopen(argv[1], "r");
	if (namefd == NULL) {
		perror("Can't open names file");
		exit(1);
	}

	/* start the list of name info's */
	filelist = (filedesc *) malloc(50 * sizeof(filedesc));
	memset(filelist, 0, 50 * sizeof(filedesc));
	if (filelist == NULL) {
		perror("Can't start files vector");
		exit(1);
	}
	/* finish the pointers */
	max_files = 50;
	debug(("First vector allocated @ %08lx, size %lu bytes\n",
		(long) filelist, 50*sizeof(filedesc)
	));
	fprintf(stderr, "build list...");

	/* this is the build loop */
	while (loc = ftell(namefd), (char_count = getline(&line_buffer, &line_size, namefd)) != -1) {
		line_buffer[char_count-1] = '\0';

		/* check for room in the buffer */
		if (n_files == max_files) {
			/* allocate more space */
			max_files += 50;
			filelist = (filedesc *) realloc(filelist, (max_files)*sizeof(filedesc));
			memset(filelist, 0, max_files * sizeof(filedesc));
			if (filelist == NULL) {
				perror("Out of memory!");
				exit(1);
			}
			debug(("Got more memory!\n"));
		}

		/* add the data for this one */
		if (lstat(line_buffer, &statbuf)) {
			fprintf(stderr, "%c  %s - ",
				(firsterr++ == 0 ? '\n' : '\r'), line_buffer
			);
			perror("ignored");
			continue;
		}

		/* check for zero length files */
		if ( statbuf.st_size == 0) {
			if (zl_hdr) {
				zl_hdr = 0;
				printf("Zero length files:\n\n");
			}
			printf("%s\n", line_buffer);
			continue;
		}

		/* If not regular file, like a directory, don't add it to filelist. 6*/
		if (S_ISREG(statbuf.st_mode) != 1) {
			fprintf(stderr, "Not regular file. ignored\n");
			continue;
		}

		curptr = filelist + n_files++;
		curptr->nameloc = loc;
		curptr->length = statbuf.st_size;
		curptr->device = statbuf.st_dev;
		curptr->inode = statbuf.st_ino;
		curptr->flags = 0;
		debug(("%cName[%ld] %s, size %ld, inode %lu\n",
			(firsttrace++ == 0 ? '\n' : '\r'), n_files, line_buffer,
			(long) statbuf.st_size, statbuf.st_ino
		));
	}

	/* sort the list by size, device, and inode */
	fprintf(stderr, "sort...");
	SORT;

	/* make the first scan for equal lengths */
	fprintf(stderr, "scan1...");
	scan1();

	/* make the second scan for dup CRC also */
	fprintf(stderr, "scan2...");
	scan2();

	fprintf(stderr, "done\n");

#ifdef DEBUG
	for (loc = 0; DebugFlg > 1 && loc < n_files; ++loc) {
		curptr = filelist + loc;
		printf("%8ld %08lx %6lu %6lu %02x\n",
			curptr->length, curptr->crc32,
			curptr->device, curptr->inode,
			curptr->flags
		);
	}
#endif

	/* now scan and output dups */
	scan3();

	/*Free the dynamically allocated memories.  6*/
	free(line_buffer);
	fclose(namefd);
	free(filelist);

	exit(0);
}

/* comp1 - compare two values */
int
comp1(p1, p2)
const void *p1, *p2;
{
	register filedesc *p1a = (filedesc *)p1, *p2a = (filedesc *)p2;
	register int retval = 0;

	if (!retval) retval = (p1a->length - p2a->length);
	if (!retval) retval = (p1a->crc32 - p2a->crc32);
	if (!retval) retval =  (p1a->device - p2a->device);
	if (!retval) retval =  (p1a->inode - p2a->inode);

	return retval;
}

/* scan1 - get a CRC32 for files of equal length */

void
scan1() {
	//FILE *fp;
	int ix, needsort = 0;

	for (ix = 1; ix <= n_files; ++ix) {
		if (filelist[ix-1].length == filelist[ix].length) {
			/* get a CRC for each */
			if (! GetFlag(ix-1, FL_CRC)) {
				filelist[ix-1].crc32 = get_crc(ix-1);
				SetFlag(ix-1, FL_CRC);
			}
			if (! GetFlag(ix, FL_CRC)) {
				filelist[ix].crc32 = get_crc(ix);
				SetFlag(ix, FL_CRC);
			}
			needsort = 1;
		}
	}

	if (needsort) SORT;
}

/* scan2 - full compare if CRC is equal */

void
scan2() {
	int ix, ix2, lastix;
	int inmatch;				/* 1st filename has been printed */
	int need_hdr = 1;			/* Need a hdr for the hard link list */
	int lnkmatch;				/* flag for matching links */
	register filedesc *p1, *p2;
	filedesc wkdesc;

	/* mark links and output before dup check */
	for (ix = 0; ix < n_files; ix = ix2) {
		p1 = filelist + ix;
		for (ix2 = ix+1, p2 = p1+1, inmatch = 0;
			ix2 < n_files
				&& p1->device == p2->device
				&& p1->inode == p2->inode;
			++ix2, ++p2
		) {
			SetFlag(ix2, FL_LNK);
			if (linkflag) {
				if (need_hdr) {
					need_hdr = 0;
					printf("\n\nHard link summary:\n\n");
				}

				if (!inmatch) {
					inmatch = 1;
					printf("\nFILE: %s\n", getfn(ix));
				}
				printf("LINK: %s\n", getfn(ix2));
			}
		}
	}
	debug(("\nStart dupscan"));

	/* now really scan for duplicates */
	for (ix = 0; ix < n_files; ix = lastix) {
		p1 = filelist + ix;
		for (lastix = ix2 = ix+1, p2 = p1+1, lnkmatch = 1;
			ix2 < n_files
				&& p1->length == p2->length
				&& p1->crc32 == p2->crc32;
			++ix2, ++p2
		) {
			if ((GetFlag(ix2, FL_LNK) && lnkmatch)
				|| fullcmp(ix, ix2) == 0
			) {
				SetFlag(ix2, FL_DUP);
				/* move if needed */
				if (lastix != ix2) {
					int n1, n2;

					debug(("\n  swap %d and %d\n", lastix, ix2));
					wkdesc = filelist[ix2];
					for (n1 = ix2; n1 > lastix; --n1) {
						filelist[n1] = filelist[n1-1];
					}
					filelist[lastix++] = wkdesc;
				}
				lnkmatch = 1;
			}
			else {
				/* other links don't match */
				lnkmatch = 0;
			}
		}
	}
}

/* scan3 - output dups */

void
scan3()
{
	register filedesc *p1, *p2;
	int ix, ix2, inmatch = 0, need_hdr = 1;
	char *headfn;				/* pointer to the filename for sups */

	/* now repeat for duplicates, links or not */
	for (ix = 0; ix < n_files; ++ix) {
		if (GetFlag(ix, FL_DUP)) {
			/* put out a header if you haven't */
			if (!inmatch)
				headfn = getfn(ix-1);
			inmatch = 1;
			if (linkflag || !GetFlag(ix, FL_LNK)) {
				/* header on the very first */
				if (need_hdr) {
					need_hdr = 0;
					printf("\n\nList of files with duplicate contents");
					if (linkflag) printf(" (includes hard links)");
					putchar('\n');
				}

				/* 1st filename if any dups */
				if (headfn != NULL) {
					/* If previous file was reg file write it wit FILE*/
					printf("\nFILE: %s\n", headfn);
					headfn = NULL;
				}
				/* Move this to above */
				printf("DUP:  %s\n", getfn(ix));
			}
		}
		else { /* If file is not duplicate. */
			inmatch = 0;
		}
	}
}

/* get_crc - get a CRC32 for a file */

uint32_t
get_crc(ix)
int ix;
{
	FILE *fp;
	char *fname;
	uint32_t retval;

	fname = getfn(ix);	/* get the file name 6*/
	fp = fopen(fname, "r");	/* open the file 6*/
	char_count = getline(&line_buffer, &line_size, fp);

	retval = rc_crc32(0, line_buffer, filelist[ix].length); /* calle the crc function that was provided by prof.*/
	fclose(fp);
	return retval;
}

/* getfn - get filename from index */

char *
getfn(ix)
off_t ix;
{
	fseek(namefd, filelist[ix].nameloc, 0);
	char_count = getline(&line_buffer, &line_size, namefd);
	/* getline method reads until \0 character is read, thus \n is included into read line.
	   This causes problem when opening files, since file name doesn't include '\n' character.*/
	line_buffer[char_count-1] = '\0';
	return line_buffer;
}

/* fullcmp - compare two files, bit for bit */

int
fullcmp(v1, v2)
int v1, v2;
{
	FILE *fp1, *fp2;
	register char ch;

	/* open the files */
	getfn(v1); /* getfn finds specific file name and saves it to global variable, called line_buffer.*/
	fp1 = fopen(line_buffer, "r");
	if (fp1 == NULL) {
		fprintf(stderr, "%s: ", line_buffer);
		perror("can't access for read");
		exit(1);
	}
	debug(("\nFull compare %s\n         and", line_buffer));

	getfn(v2); /* getfn finds specific file name and saves it to global variable, called line_buffer.*/
	fp2 = fopen(line_buffer, "r");
	if (fp2 == NULL) {
		fprintf(stderr, "%s: ", line_buffer);
		perror("can't access for read");
		exit(1);
	}
	debug(("%s", line_buffer));

	/* now do the compare */
	while ((ch = getc(fp1)) != EOF) {
		if (ch - getc(fp2)) break;
	}

	/* close files and return value */
	fclose(fp1);
	fclose(fp2);
	debug(("\n      return %d", !(ch == EOF)));
	return (!(ch == EOF));
}

/*
 * Helper function to parse passed argument to an integer. Allows us to see of passed argument is valid int or not.
 * Return: int value of passed string or -1 if invalid str is passed.
 */
int str_to_int(char *str) {
	int num = 0;
	int read_char;
	char *current_char = str;

	read_char = *current_char;

	while(read_char != '\0') {
		if(read_char >= 48 && read_char <= 57) {
			num *= 10;
			num += read_char-48;
			current_char++;
			read_char = *current_char;
		} else {
			return -1;
		}
	}
	return num;
}