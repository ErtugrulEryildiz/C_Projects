This project uses a command line utility to synthesize audio data containing Dual-Tone Multi-Frequency (DTMF) signals and to detect DTMF signals in audio data by utulizing Goertzel Algorithm.


USAGE: bin/dtmf [-h] -g|-d [-t MSEC] [-n NOISE_FILE] [-l LEVEL] [-b BLOCKSIZE]
-h 	Help: displays this help menu.
-g 	Generate: read DTMF events from standard input, output audio data to standard output.
-d 	Detect: read audio data from standard input, output DTMF events to standard output.

	Optional additional parameters for -g (not permitted with -d):
-t 	MSEC Time duration (in milliseconds, default 1000) of the audio output.
-n 	NOISE_FILE specifies the name of an audio file containing "noise" to be combined with the synthesized DTMF tones.
-l 	LEVEL specifies the loudness ratio (in dB, positive or negative) of the noise to that of the DTMF tones. A LEVEL of 0 (the default) means the same level, negative values mean that the DTMF tones are louder than the noise, positive values mean that the noise is louder than the DTMF tones.

	Optional additional parameter for -d (not permitted with -g):
-b 	BLOCKSIZE specifies the number of samples (range [10, 1000], default 100) in each block of audio to be analyzed for the presence of DTMF tones.


Valid Command Input:

$ bin/dtmf -g -t 100 < dtmf.txt > audio.au