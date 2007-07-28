//
// DRO2MIDI - Convert DOSBox Raw OPL captures (.dro) into MIDI files (.mid)
//
// Based on imf2midi v1.0 written by Guenter Nagler 1996 (gnagler@ihm.tu-graz.ac.at)
//
// IMF processor replaced with DRO processor by malvineous@shikadi.net (June 2007)
//
// Instrument mappings are stored in *.reg ASCII files.  These files describe
// the Adlib instruments and which MIDI patch they correspond to.
//
//  v1.0 / 2007-06-16 / malvineous@shikadi.net: Original release
//  v1.1 / 2007-07-28 / malvineous@shikadi.net: Added .imf and .raw support, replaced
//     Guenter's OPL -> MIDI frequency conversion algorithm (from a lookup table into
//     a more accurate formula), consequently could simplify pitchbend code (now
//     conversions with pitchbends enabled sound quite good!)
//

#include "midiio.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>
#include "freq.hpp"
//#include <dir.h>

#ifdef __MSDOS__
#define WRITE_BINARY  "wb"
#define READ_TEXT     "r"
#else
#define WRITE_BINARY  "w"
#define READ_TEXT     "r"
#endif

//#define PITCHBEND
int pitchbend_center = 0x1000L;

bool bRhythm = false; // convert rhythm mode instruments (-r)
bool bUsePitchBends = false; // use pitch bends to better match MIDI note frequency with the OPL frequency (-p)

char* input = 0;
char* output = 0;

FILE* f = 0;  // input file
MidiWrite* write = 0;

//int resolution = 384; // 560Hz IMF
int resolution = 500;   // 1000Hz DRO
int program = 0;
float tempo = 120.0;
int mapchannel[9];

typedef struct
{
  unsigned char reg20[2];
  unsigned char reg40[2];
  unsigned char reg60[2];
  unsigned char reg80[2];
  unsigned char regC0;
  unsigned char regE0[2];

  int prog;
  int isdrum;
  char name[8+1+3+1];
	
	int iOctave; // need to store octave for rhythm mode instruments
} INSTRUMENT;

INSTRUMENT reg[9]; // current registers of channel
int lastprog[9];

int iFormat = 0; // input format
#define FORMAT_IMF  1
#define FORMAT_DRO  2
#define FORMAT_RAW  3
int iSpeed = 0; // clock speed (in Hz)

void usage()
{
  fprintf(stderr,
		"DRO2MIDI converts DOSBox .dro captures to General MIDI\n"
		"Written by malvineous@shikadi.net in 2007 (v1.2)\n"
		"Heavily based upon IMF2MIDI written by Guenter Nagler in 1996\n"
		"\n"
		"Usage: dro2midi [-p] [-r] input.dro output.mid\n"
		"\n"
		"Where:\n"
		"\n"
		"  -p   Use MIDI pitch bends to more accurately match the OPL note frequency\n"
		"  -r   Also convert OPL rhythm-mode instruments\n"
		"\n"
		"Supported input formats:\n"
		"\n"
		" .raw  Rdos RAW OPL capture\n"
		" .dro  DOSBox RAW OPL capture\n"
		" .imf  id Software Music Format (type-0 and type-1 at 560Hz)\n"
		" .wlf  id Software Music Format (type-0 and type-1 at 700Hz)\n"
		"\n"
		"Instrument definitions are read in from iX.reg, where X starts at 1 and\n"
		"increases until file-not-found.\n"
	);
  exit(1);
}

int getchannel(int i)
{
/*
   Channel        1   2   3   4   5   6   7   8   9
   Operator 1    00  01  02  08  09  0A  10  11  12
   Operator 2    03  04  05  0B  0C  0D  13  14  15
*/
  switch(i)
  {
  case 0: return 0;
  case 1: return 1;
  case 2: return 2;
  case 3: return 0;
  case 4: return 1;
  case 5: return 2;
  case 8: return 3;
  case 9: return 4;
  case 0xa: return 5;
  case 0xb: return 3;
  case 0xc: return 4;
  case 0xd: return 5;
  case 0x10: return 6;
  case 0x11: return 7;
  case 0x12: return 8;
  case 0x13: return 6;
  case 0x14: return 7;
  case 0x15: return 8;
  default:
    printf("no channel for register %02X found\n", i);
    break;
  }
  return 0; // not valid
}

int getop(int i)
{
  switch(i)
  {
  case 0: return 0;
  case 1: return 0;
  case 2: return 0;
  case 3: return 1;
  case 4: return 1;
  case 5: return 1;
  case 8: return 0;
  case 9: return 0;
  case 0xa: return 0;
  case 0xb: return 1;
  case 0xc: return 1;
  case 0xd: return 1;
  case 0x10: return 0;
  case 0x11: return 0;
  case 0x12: return 0;
  case 0x13: return 1;
  case 0x14: return 1;
  case 0x15: return 1;
  default:
    printf("no op for register %02X found\n", i);
    break;
  }
  return 0; // not valid
}


int loadinstr(char* filename, INSTRUMENT& in)
{
FILE* f = fopen(filename, "r");
char line[128];
  if (!f)
    return 0;
  memset(&in, 0, sizeof(in));
  strcpy(in.name, filename);
  if (strncasecmp(filename, "DRUM", 4) == 0)
    in.isdrum = 1;
  while (fgets(line, sizeof(line)-1, f))
  {
    int code, param1, param2;

    if (sscanf(line, "%02X: %02X %02X", &code, &param1, &param2) == 3)
    {
      if (code >= 0x20 && code <= 0x35)
      {
	in.reg20[0] = param1;
	in.reg20[1] = param2;
      }
      else if (code >= 0x40 && code <= 0x55)
      {
	in.reg40[0] = param1;
	in.reg40[1] = param2;
      }
      else if (code >= 0x60 && code <= 0x75)
      {
	in.reg60[0] = param1;
	in.reg60[1] = param2;
      }
      else if (code >= 0x80 && code <= 0x95)
      {
	in.reg80[0] = param1;
	in.reg80[1] = param2;
      }
      else if (code >= 0xE0 && code <= 0xF5)
      {
	in.regE0[0] = param1;
	in.regE0[1] = param2;
      }
      else
	fprintf(stderr, "unknown register: %s\n", line);
    }
    else if (sscanf(line, "%02X: %02X", &code, &param1)  == 2)
    {
      if (code >= 0xC0 && code <= 0xC8)
	in.regC0 = param1;
      else
	fprintf(stderr, "unknown register: %s\n", line);
    }
    else if (strncasecmp(line, "program:", 8) == 0)
    {
      in.prog = atoi(line+8);
      in.isdrum = 0;
    }
    else
      fprintf(stderr, "invalid line: %s\n", line);
  }
  fclose(f);
  return 1;
}

#define MAXINSTR  1000
int instrcnt = 0;
INSTRUMENT instr[MAXINSTR];

void loadinstruments()
{
	// IMF2MIDI originally read in any file ending in .reg, but I can't be
	// bothered writing a cross-platform directory reader (or using an
	// existing one) so I'll just make it read in .reg files numerically
	// starting at 1.reg, 2.reg, etc. until it finds one that doesn't exist.
	char cName[50];
	for (int iIndex = 1;; iIndex++) {
		sprintf(cName, "i%d.reg", iIndex);
		printf("Reading %s...\n", cName);
		if (loadinstr(cName, instr[instrcnt])) {
			instrcnt++;
			assert(instrcnt < MAXINSTR);
		} else {
			printf("Error reading %s, assuming all instruments have been read in now.\n", cName);
			break;
		}
	}
	// Same again for drumX.reg
	for (int iIndex = 1;; iIndex++) {
		sprintf(cName, "drum%d.reg", iIndex);
		printf("Reading %s...\n", cName);
		if (loadinstr(cName, instr[instrcnt])) {
			instrcnt++;
			assert(instrcnt < MAXINSTR);
		} else {
			printf("Error reading %s, assuming all drum instruments have been read in now.\n", cName);
			break;
		}
	}
/*
	int done;
struct ffblk ff;

  done = findfirst("*.reg", &ff, 0);
  while (!done)
  {
    if (loadinstr(ff.ff_name, instr[instrcnt]))
      instrcnt++;
    done = findnext(&ff);
  }
*/
}

long difference(int a, int b, int importance = 1)
{
  long diff = a-b;
  if (diff < 0)
    diff = -diff;
  return diff * importance;
}

long compareinstr(INSTRUMENT & a, INSTRUMENT  & b)
{
  return
     difference(a.reg20[0], b.reg20[0], 2) +
     difference(a.reg20[1], b.reg20[1], 2) +
     difference(a.reg40[0], b.reg40[0], 1) +
     difference(a.reg40[1], b.reg40[1], 1) +
     difference(a.reg60[0], b.reg60[0], 2) +
     difference(a.reg60[1], b.reg60[1], 2) +
     difference(a.reg80[0], b.reg80[0], 2) +
     difference(a.reg80[1], b.reg80[1], 2) +
     difference(a.regC0, b.regC0, 3) +
     difference(a.regE0[0], b.regE0[0], 1) +
     difference(a.regE0[1], b.regE0[1], 1);
}

int findinstr(int channel)
{
int besti = -1;
long bestdiff = 0;

  for (int i = 0; i<  instrcnt; i++)
  {
    long diff = compareinstr(instr[i], reg[channel]);
    if (besti < 0 || diff < bestdiff)
    {
      bestdiff = diff;
      besti = i;
      if (bestdiff == 0)
	break;
    }
  }
	if (bestdiff != 0) {
		// Couldn't find an exact match, print the details
	 fprintf(stderr, "No match in any .reg file for instrument on channel %d:\n--- Begin iX.reg ---\n", channel);
	 printf("Program: ?\n");
	 printf("20: %02X %02X\n", reg[channel].reg20[0], reg[channel].reg20[1]);
	 printf("40: %02X %02X\n", reg[channel].reg40[0], reg[channel].reg40[1]);
	 printf("60: %02X %02X\n", reg[channel].reg60[0], reg[channel].reg60[1]);
	 printf("80: %02X %02X\n", reg[channel].reg80[0], reg[channel].reg80[1]);
	 printf("C0: %02X\n", reg[channel].regC0);
	 printf("E0: %02X %02X\n", reg[channel].regE0[0], reg[channel].regE0[1]);
	 printf("--- End iX.reg ---\n");
	 
	 // Save this unknown instrument as a known one, so the registers don't get printed again
	 reg[channel].prog = instr[besti].prog;  // but keep the same patch that we've already assigned, so it doesn't drop back to a piano for the rest of the song
	 instr[instrcnt++] = reg[channel];
	}
  return besti;
}

int main(int argc, char**argv)
{
int c;

  argc--; argv++;
  while (argc > 0 && **argv == '-')
  {
		if (strncasecmp(*argv, "-r", 2) == 0) {
			::bRhythm = true;
		} else if (strncasecmp(*argv, "-p", 2) == 0) {
			::bUsePitchBends = true;
		} else {
			fprintf(stderr, "invalid option %s\n", *argv);
	    usage();
		}
    argc--; argv++;
  }
  if (argc < 2)
    usage();

  input = argv[0];
  output = argv[1];
  if (strcmp(input, output) == 0)
  {
    fprintf(stderr, "cannot convert to same file\n");
    return 1;
  }

  f = fopen(input, READ_BINARY);
  if (!f)
  {
    perror(input);
    return 1;
  }
  unsigned long imflen = 0;

	char cSig[9];
	fseek(f, 0, SEEK_SET);
  fgets(cSig, 9, f);
	if (strncmp(cSig, "DBRAWOPL", 8) == 0) {
		::iFormat = FORMAT_DRO;
		printf("Input file is in DOSBox DRO format.\n");
		::iSpeed = 1000;

		fseek(f, 16, SEEK_SET); // seek to "length in bytes" field
	  imflen = fgetc(f);
	  imflen += fgetc(f) << 8L;
	  imflen += fgetc(f) << 16L;
	  imflen += fgetc(f) << 24;
	} else if (strncmp(cSig, "RAWADATA", 8) == 0) {
		::iFormat = FORMAT_RAW;
		fprintf(stderr, "Input file is in Rdos RAW format.  This is not yet supported.\n");

		// Read until EOF (0xFFFF is really the end but we'll check that during conversion)
		fseek(f, 0, SEEK_END);
	  imflen = ftell(f);

		fseek(f, 8, SEEK_SET); // seek to "length in bytes" field
		int iClockSpeed = fgetc(f) + (fgetc(f) << 8L);
		if ((iClockSpeed == 0) || (iClockSpeed == 0xFFFF)) {
			::iSpeed = 1000; // default to 1000Hz
		} else {
			::iSpeed = (int)(1193180.0 / iClockSpeed);
		}
	} else {
		::iFormat = FORMAT_IMF;
		if ((cSig[0] == 0) && (cSig[1] == 0)) {
			printf("Input file appears to be in IMF type-0 format.\n");
			fseek(f, 0, SEEK_END);
		  imflen = ftell(f);
			fseek(f, 0, SEEK_SET);
		} else {
			printf("Input file appears to be in IMF type-1 format.\n");
		  imflen = cSig[0] + (cSig[1] << 8);
			fseek(f, 2, SEEK_SET);
		}
		if (strcasecmp(&input[strlen(input)-3], "imf") == 0) {
			printf("File extension is .imf - using 560Hz speed (rename to .wlf if this is too slow)\n");
			::iSpeed = 560;
		} else if (strcasecmp(&input[strlen(input)-3], "wlf") == 0) {
			printf("File extension is .wlf - using 700Hz speed (rename to .imf if this is too fast)\n");
			::iSpeed = 700;
		} else {
			printf("Unknown file extension - must be .imf or .wlf\n");
			return 3;
		}
	}

  write = new MidiWrite(output);
  if (!write)
  {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  if (!write->getf())
  {
    perror(output);
    return 1;
  }
	int iInitialSpeed = ::iSpeed;
	resolution = iInitialSpeed / 2;
  write->head(/* version */ 0, /* track count updated later */0, resolution);


  write->track();
  write->tempo((long)(60000000.0 / tempo));
  write->tact(4,4,24,8);

  for (c = 0; c  <= 8; c++)
    lastprog[c] = -1;

  loadinstruments();

  printf("guessing instruments:\n");
  for (c = 0; c <= 8; c++)
  {
    write->volume(c, 127);
//    write->program(c, c);
    lastprog[c] = -1;
		reg[c].iOctave = 0;
  }

  int delay = 0;
  int octave = 0;
  int curfreq[9];
  bool keyAlreadyOn[9];
	int lastkey[9];
  int channel;
  int code, param;
	
	int pitchbent[9];
	
	int rhythm[5]; // are these rhythm instruments currently playing?
  for (c = 0; c < 5; c++) rhythm[c] = 0;

  for (c = 0; c < 9; c++)
  {
    curfreq[c] = 0;
    mapchannel[c] = c;
		keyAlreadyOn[c] = false;
		lastkey[c] = -1;
		pitchbent[c] = pitchbend_center;
  }
	int iMinLen; // Minimum length for valid notes to still be present
	switch (::iFormat) {
		case FORMAT_IMF: iMinLen = 4; break;
		case FORMAT_DRO: iMinLen = 2; break;
		case FORMAT_RAW: iMinLen = 2; break;
	}

	unsigned long iSize = imflen; // sometimes the counter wraps around, need this to stop it from happening
  while ((imflen >= iMinLen) && (imflen <= iSize))
  {
		switch (::iFormat) {
			case FORMAT_IMF:
				// Write the last iteration's delay (since the delay needs to come *after* the note)
		    write->time(delay);
				
		    code = fgetc(f);
		    param = fgetc(f);
				delay = fgetc(f); delay += (fgetc(f) << 8);
		    imflen-=4;
				break;
			case FORMAT_DRO:
		    code = fgetc(f);
				imflen--;
				switch (code) {
					case 0x00: // delay (byte)
						delay += 1 + fgetc(f);
						imflen--;
						continue;
					case 0x01: // delay (int)
						delay += 1 + fgetc(f);
						delay += fgetc(f) << 8L;
						imflen -= 2;
						continue;
					case 0x02: // use first OPL chip
					case 0x03: // use second OPL chip
						fprintf(stderr, "Warning: This song uses multiple OPL chips - this isn't yet supported!\n");
						continue;
					case 0x04: // escape
						code = fgetc(f);
						imflen--;
						break;
				}
		    param = fgetc(f);
				imflen--;
				
				// Write any delay (as this needs to come *before* the next note)
		    write->time(delay);
				delay = 0;
				break;
			case FORMAT_RAW:
		    param = fgetc(f);
		    code = fgetc(f);
				imflen-=2;
				switch (code) {
					case 0x00: // delay (byte)
						delay += param;//fgetc(f);
						//imflen--;
						continue;
					case 0x02: // delay (int)
						switch (param) {
							case 0x00:
								if (delay != 0) {
									// See below - we need to write out any delay at the old clock speed before we change it
							    write->time((delay * iInitialSpeed / ::iSpeed));
									delay = 0;
								}
								::iSpeed = (int)(1193180.0 / (fgetc(f) | (fgetc(f) << 8L)));
								//printf("Speed set to %d\n", iSpeed);
								imflen -= 2;
								break;
							case 0x01:
							case 0x02:
								printf("Switching OPL ports is not yet implemented!\n");
								break;
						}
						continue;
					case 0xFF:
						if (param == 0xFF) {
							// End of song
							imflen = 0;
							continue;
						}
						break;
				}
				
				// Write any delay (as this needs to come *before* the next note)
				// Since our global clock speed is 1000Hz, we have to multiply this
				// delay accordingly as the delay units are in the current clock speed.
				// This calculation converts them into 1000Hz delay units regardless of
				// the current clock speed.
		    write->time((delay * iInitialSpeed / ::iSpeed));
				//printf("delay is %d (ticks %d)\n", (delay * iInitialSpeed / ::iSpeed), delay);
				delay = 0;
				break;
		} // switch (::iFormat)

    if (code >= 0xa0 && code <= 0xa8) // set freq bits 0-7
    {
      channel = code-0xa0;
      curfreq[channel] = (curfreq[channel] & 0xF00) + (param & 0xff);
      continue;
    }
    else if (code >= 0xB0 && code <= 0xB8) // set freq bits 8-9 and octave and on/off
    {
      channel = code - 0xb0;
      curfreq[channel] = (curfreq[channel] & 0x0FF) + ((param & 0x03)<<8);
      octave = (param >> 2) & 7;
			reg[channel].iOctave = octave; // save in case rhythm instruments will be sounding on this channel
      int keyon = (param >> 5) & 1;

      //int key = freq2key(curfreq[channel], octave);
      double keyFrac = freq2key(curfreq[channel], octave);
      int key = (int)round(keyFrac);
			//printf("key: %lf\n", key);
      if ((key > 0) && (keyon)) {

				if (keyAlreadyOn[channel]) {
					// There's already a note playing on this channel, just worry about the pitch of that

	    	  if (::bUsePitchBends) {
						double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999
						if ((keyAlreadyOn[channel]) && (lastkey[channel] != key)) {
							// Frequency has changed while note is playing
						
							// Start a pitch slide from the old note...
							write->portamentotime(mapchannel[channel], lastkey[channel]);
							printf("event: portamento to note\n");
							// ...and slide to the next note played
						} else {
							// It's the same note, but the pitch is off just slightly
							int iNewBend = (int)(pitchbend_center + 0x1000L * dbDiff);
					    if (iNewBend != pitchbent[channel]) {
								write->pitchbend(mapchannel[channel], pitchbent[channel]); // pitchbends are between 0x0000L and 0x2000L
								pitchbent[channel] = iNewBend;
							}
						}
					} else {
						// We're not using pitchbends, so just switch off the note
						write->noteoff(mapchannel[channel], lastkey[channel]);
						lastkey[channel] = 0;
						keyAlreadyOn[channel] = false;
					}
				}

				//} else {
				if ((!keyAlreadyOn[channel]) || (::bUsePitchBends)) {  // If *now* there's no note playing...
					// See if we need to update anything

					// See if the instrument needs to change
					int i = findinstr(channel);
					if (i >= 0 && instr[i].prog != lastprog[channel]) {
						printf("channel %d set to: %s\n", channel, instr[i].name);
						if (instr[i].prog >= 0 && !instr[i].isdrum && mapchannel[channel] == channel)
							write->program(mapchannel[channel], lastprog[channel] = instr[i].prog);
						else {
							mapchannel[channel] = gm_drumchannel;
							lastprog[channel] = instr[i].prog;
						}
					}
					
					// Play the note
	    	  if ((::bUsePitchBends) && (!keyAlreadyOn[channel])) {
						double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999

						int iNewBend = (int)(pitchbend_center + 0x1000L * dbDiff);
				    if (iNewBend != pitchbent[channel]) {
							write->pitchbend(mapchannel[channel], pitchbent[channel]); // pitchbends are between 0x0000L and 0x2000L
							pitchbent[channel] = iNewBend;
						}
					}
					int level = reg[channel].reg40[0] & 0x3f;
					if (level > (reg[channel].reg40[1] & 0x3f))
					level = reg[channel].reg40[1] & 0x3f;
					write->noteon(mapchannel[channel], key, (0x3f - level) << 1);
					
					if ((::bUsePitchBends) && (keyAlreadyOn[channel])) {
						// There was a note already playing, so we probably portamento'd to this note.
						// We still need to switch off the old note.
						write->noteoff(mapchannel[channel], lastkey[channel]);
					}
					lastkey[channel] = key;
					keyAlreadyOn[channel] = true;
					
				}

      } else {
				write->noteoff(mapchannel[channel], lastkey[channel]);
				lastkey[channel] = 0;
				keyAlreadyOn[channel] = false;
			}
    }
		else if ((code == 0xBD) && (::bRhythm))
		{
			#define KEY_BASSDRUM  36 // MIDI bass drum
			#define KEY_SNAREDRUM 38 // MIDI snare drum
			#define KEY_TOMTOM    41 // MIDI tom 1
			#define KEY_TOPCYMBAL 57 // MIDI crash cymbal
			#define KEY_HIHAT     42 // MIDI closed hihat

			#define CHAN_BASSDRUM  6 // Bass drum sits on OPL channel 7
			#define CHAN_TOMTOM    8 // Tom tom sits on OPL channel 9 (modulator only)
			
			#define PATCH_BASSDRUM 116 // MIDI Taiko drum
			#define PATCH_TOMTOM   117 // MIDI melodic drum
			
			// TODO: Correlate 'total level' (reg40) with the operator for the particular rhythm instrument
/*						int level = reg[channel].reg40[0] & 0x3f;
						if (level > (reg[channel].reg40[1] & 0x3f))
						level = reg[channel].reg40[1] & 0x3f;*/
			int level = 0x00;
			// Rhythm mode instruments
			if ((param >> 5) & 1) {
				// Rhythm mode is active
				if ((param >> 4) & 1) {
					// Bass drum

					int channel = CHAN_BASSDRUM;
					int level = reg[channel].reg40[0] & 0x3f;
					if (level > (reg[channel].reg40[1] & 0x3f))
					level = reg[channel].reg40[1] & 0x3f;

					if (lastprog[channel] != PATCH_BASSDRUM) {
						printf("channel %d: Rhythm-mode bass drum\n", channel);
						write->program(mapchannel[channel], PATCH_BASSDRUM);
//						mapchannel[channel] = gm_drumchannel;
						lastprog[channel] = PATCH_BASSDRUM;
					}

		      double keyFrac = freq2key(curfreq[channel], octave);
		      int key = (int)round(keyFrac);

	    	  if (::bUsePitchBends) {
						double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999
						//printf("diff: %lf\n", dbDiff);
				    write->pitchbend(channel, (int)(pitchbend_center + 0x2000L * dbDiff));
					}

//		      int key = freq2key(curfreq[channel], reg[channel].iOctave);
					//write->noteon(gm_drumchannel, KEY_BASSDRUM, (0x3f - level) << 1);
					write->noteon(channel, key, (0x3f - level) << 1);

					rhythm[4] = 1;
				} else if (rhythm[4]) {
					// Bass drum off
					write->noteoff(gm_drumchannel, KEY_BASSDRUM);
					rhythm[4] = 0;
				}
				if ((param >> 3) & 1) {
					// Snare drum
					write->noteon(gm_drumchannel, KEY_SNAREDRUM, (0x3f - level) << 1);
					rhythm[3] = 1;
				} else if (rhythm[3]) {
					// Snare drum off
					write->noteoff(gm_drumchannel, KEY_SNAREDRUM);
					rhythm[3] = 0;
				}
				if ((param >> 2) & 1) {
					// Tom tom

					int channel = CHAN_TOMTOM;
					int level = reg[channel].reg40[0] & 0x3f;
					if (level > (reg[channel].reg40[1] & 0x3f))
					level = reg[channel].reg40[1] & 0x3f;

					if (lastprog[channel] != PATCH_TOMTOM) {
						printf("channel %d: Rhythm-mode bass drum\n", channel);
						write->program(mapchannel[channel], PATCH_TOMTOM);
//						mapchannel[channel] = gm_drumchannel;
						lastprog[channel] = PATCH_TOMTOM;
					}

		      double keyFrac = freq2key(curfreq[channel], octave);
		      int key = (int)round(keyFrac);

	    	  if (::bUsePitchBends) {
						double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999
						//printf("diff: %lf\n", dbDiff);
				    write->pitchbend(channel, (int)(pitchbend_center + 0x2000L * dbDiff));
					}
//		      int key = freq2key(curfreq[channel], reg[channel].iOctave);
					//write->noteon(gm_drumchannel, KEY_BASSDRUM, (0x3f - level) << 1);
					write->noteon(channel, key, (0x3f - level) << 1);

//					write->noteon(gm_drumchannel, KEY_TOMTOM, (0x3f - level) << 1);
					rhythm[2] = 1;
				} else if (rhythm[2]) {
					// Tom tom off
					write->noteoff(gm_drumchannel, KEY_TOMTOM);
					rhythm[2] = 0;
				}
				if ((param >> 1) & 1) {
					// Top cymbal
					write->noteon(gm_drumchannel, KEY_TOPCYMBAL, (0x3f - level) << 1);
					rhythm[1] = 1;
				} else if (rhythm[1]) {
					// Top cymbal off
					write->noteoff(gm_drumchannel, KEY_TOPCYMBAL);
					rhythm[1] = 0;
				}
				if (param & 1) {
					// Hi hat
					write->noteon(gm_drumchannel, KEY_HIHAT, (0x3f - level) << 1);
					rhythm[0] = 1;
				} else if (rhythm[0]) {
					// Hi hat off
					write->noteoff(gm_drumchannel, KEY_HIHAT);
					rhythm[0] = 0;
				}
			} else {
				// Rhythm is off, make sure all the instruments are too
				if (rhythm[4]) { write->noteoff(gm_drumchannel, KEY_BASSDRUM);  rhythm[4] = 0; }
				if (rhythm[3]) { write->noteoff(gm_drumchannel, KEY_SNAREDRUM); rhythm[3] = 0; }
				if (rhythm[2]) { write->noteoff(gm_drumchannel, KEY_TOMTOM);    rhythm[2] = 0; }
				if (rhythm[1]) { write->noteoff(gm_drumchannel, KEY_TOPCYMBAL); rhythm[1] = 0; }
				if (rhythm[0]) { write->noteoff(gm_drumchannel, KEY_HIHAT);     rhythm[0] = 0; }
			}
		}
    else if (code >= 0x20 && code <= 0x35)
    {
      channel = getchannel(code-0x20);
      reg[channel].reg20[getop(code-0x20)] = param;
    }
    else if (code >= 0x40 && code <= 0x55)
    {
      channel = getchannel(code-0x40);
      reg[channel].reg40[getop(code-0x40)] = param;
    }
    else if (code >= 0x60 && code <= 0x75)
    {
      channel = getchannel(code-0x60);
      reg[channel].reg60[getop(code-0x60)] = param;
    }
    else if (code >= 0x80 && code <= 0x95)
    {
      channel = getchannel(code-0x80);
      reg[channel].reg80[getop(code-0x80)] = param;
    }
    else if (code >= 0xc0 && code <= 0xc8)
    {
      channel = code-0xc0;
      reg[channel].regC0 = param;
    }
    else if (code >= 0xe0 && code <= 0xF5)
    {
      channel = getchannel(code-0xe0);
      reg[channel].regE0[getop(code-0xe0)] = param;
    }
  }

  delete write;
  fclose(f);
  return 0;
}
