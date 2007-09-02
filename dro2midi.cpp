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
//
//  v1.1 / 2007-07-28 / malvineous@shikadi.net: More file formats
//     - Added .imf and .raw support
//     - Replaced Guenter's OPL -> MIDI frequency conversion algorithm (from a
//       lookup table into a much more accurate formula), consequently was able
//       to simplify pitchbend code (now conversions with pitchbends enabled
//       sound quite good!)
//
//  v1.2 / 2007-07-28 / malvineous@shikadi.net: Bugfix release
//     - Fixed some file length calculations causing some files to be converted
//       without any notes
//     - Added portamento-to-note for large (>2 semitone) pitchbends, but it
//       doesn't seem to work when using Timidity.
//
//  v1.3 / 2007-09-02 / malvineous@shikadi.net: New features
//     - Fixed "tom tom" incorrectly called "bass drum" in output messages.
//     - Fixed multi-note pitchbends by removing portamento-to-note and
//       adjusting standard pitchbend range instead, thanks to a suggestion
//       by Xky (xkyrauh2001@hotmail.com)
//     - Implemented a better method for reading Adlib register -> MIDI patch
//       mapping information (all stored in inst.txt now instead of having a
//       seperate file for each instrument.)  Also improved method for mapping
//       instruments to percussion on MIDI channel 10.
//     - Fixed OPL rhythm instrument conversion issue (a MIDI noteon was being
//       generated too often - if the OPL instrument is on and we receive
//       another keyon, it *shouldn't* generate a fresh MIDI keyon.)
//     - Fixed IMF type-1 conversion issue where unsigned numbers were being
//       read as signed, and the conversion was cutting off half way through.
//
// TODO: Figure out why the Stunts .raw captures don't convert at all
// (see testcases/stunts.raw)

#define VERSION "1.3"
#define MAPPING_FILE "inst.txt"

#define PATCH_NAME_FILE "patch.txt"
#define PERC_NAME_FILE "drum.txt"
#define NUM_MIDI_PATCHES  128  // 128 MIDI instruments
#define NUM_MIDI_PERC     128  // 46 MIDI percussive notes (channel 10), but 128 possible notes
#define INSTR_NAMELEN      32  // Maximum length of an instrument name

//#define PITCHBEND_RANGE 12.0   // 12 == pitchbends can go up to a full octave
#define PITCHBEND_RANGE 24.0   // 24 == pitchbends can go up two full octaves
#define PITCHBEND_ONESEMITONE  (8192.0 / PITCHBEND_RANGE)
const double pitchbend_center = 8192.0;

#include "midiio.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>
#include "freq.hpp"
#include <stdio.h>
//#include <dir.h>

#define WRITE_BINARY  "wb"
#define READ_TEXT     "r"

#ifdef _MSC_VER
// Keep MS VC++ happy
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#define snprintf _snprintf
#define log2(x) logbase(x, 2)
inline double round( double d )
{
return floor( d + 0.5 );
}
#endif

bool bRhythm = true; // convert rhythm mode instruments (-r)
bool bUsePitchBends = true; // use pitch bends to better match MIDI note frequency with the OPL frequency (-p)
bool bApproximatePitchbends = false; // if pitchbends are disabled, should we approximate them by playing the nearest note when the pitch changes?
bool bPerfectMatchesOnly = false;  // if true, only match perfect instruments


//#define KEY_BASSDRUM  36 // MIDI bass drum
#define KEY_SNAREDRUM 38 // MIDI snare drum
//#define KEY_TOMTOM    41 // MIDI tom 1
#define KEY_TOPCYMBAL 57 // MIDI crash cymbal
#define KEY_HIHAT     42 // MIDI closed hihat

#define CHAN_BASSDRUM  6 // Bass drum sits on OPL channel 7
#define CHAN_TOMTOM    8 // Tom tom sits on OPL channel 9 (modulator only)
			
#define PATCH_BASSDRUM 116 // MIDI Taiko drum
#define PATCH_TOMTOM   118//117 // MIDI melodic drum

char cPatchName[NUM_MIDI_PATCHES][INSTR_NAMELEN];
char cPercName[NUM_MIDI_PERC][INSTR_NAMELEN];

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

  int prog;  // MIDI patch (or -1 if a drum)
  int isdrum;
	int note; // note to play if drum
//  char name[8+1+3+1];
	char name[128];
	
	int redirect; // if >= 0, use ::instr[redirect] instead of this one
	
	int iOctave; // need to store octave for rhythm mode instruments
} INSTRUMENT;

INSTRUMENT reg[9]; // current registers of channel
int lastprog[9];

int iFormat = 0; // input format
#define FORMAT_IMF  1
#define FORMAT_DRO  2
#define FORMAT_RAW  3
int iSpeed = 0; // clock speed (in Hz)
int iInitialSpeed = 0; // first iSpeed value written to MIDI header

void version()
{
  printf("DRO2MIDI v" VERSION " - Convert raw Adlib captures to General MIDI\n"
		"Written by malvineous@shikadi.net in 2007\n"
		"Heavily based upon IMF2MIDI written by Guenter Nagler in 1996\n"
		"http://www.shikadi.net/utils/\n"
		"\n"
	);
	return;
}

void usage()
{
	version();
  fprintf(stderr,
		"Usage: dro2midi [-p [-a]] [-r] input.dro output.mid\n"
		"\n"
		"Where:\n"
		"  -p   Disable use of MIDI pitch bends\n"
		"  -a   If pitchbends are disabled, approximate by playing the nearest note\n"
		"  -r   Don't convert OPL rhythm-mode instruments\n"
		"  -i   Only use instruments that match perfectly (default is 'close enough is\n"
		"       good enough.') Useful when guessing new patches. Instruments that can't\n"
		"       be matched use the first entry in " MAPPING_FILE " (piano by default)\n"
		"\n"
		"Supported input formats:\n"
		" .raw  Rdos RAW OPL capture\n"
		" .dro  DOSBox RAW OPL capture\n"
		" .imf  id Software Music Format (type-0 and type-1 at 560Hz)\n"
		" .wlf  id Software Music Format (type-0 and type-1 at 700Hz)\n"
		"\n"
		"Instrument definitions are read in from " MAPPING_FILE ".  Instrument names\n"
		"are read in from " PATCH_NAME_FILE " and " PERC_NAME_FILE ".\n"
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


#define MAXINSTR  1000
int instrcnt = 0;
INSTRUMENT instr[MAXINSTR];


bool loadInstruments(void)//char* filename, INSTRUMENT& in)
{
	for (int i = 0; i < NUM_MIDI_PATCHES; i++) sprintf(cPatchName[i], "Patch #%d", i+1);
	for (int i = 0; i < NUM_MIDI_PERC; i++) sprintf(cPercName[i], "Note #%d", i);

	char line[256];

	FILE* p = fopen(PATCH_NAME_FILE, "r");
  if (!p) {
		fprintf(stderr, "Warning: Unable to open file listing patch names (" PATCH_NAME_FILE ")\nInstrument names will not be available.\n");
	} else {
		while (fgets(line, sizeof(line)-1, p)) {
			int iValue, iLen;
			char *p = strpbrk(line, "\n\r");
			if (p) *p = '\0'; // terminate the string at the newline
			if (sscanf(line, "%d=%n", &iValue, &iLen) == 1) {
				assert(iValue <= NUM_MIDI_PATCHES);
				snprintf(cPatchName[iValue-1], INSTR_NAMELEN, "%s [%d]", &line[iLen], iValue);
			} else if ((line[0] != '#') && (line[0] != '\n')) {
				fprintf(stderr, "Invalid line in " PATCH_NAME_FILE ": %s\n", line);
			}
		}
		fclose(p);
	}

	p = fopen(PERC_NAME_FILE, "r");
  if (!p) {
		fprintf(stderr, "Warning: Unable to open file listing percussion note names (" PERC_NAME_FILE ")\nPercussion names will not be available.\n");
	} else {
		while (fgets(line, sizeof(line)-1, p)) {
			int iValue, iLen;
			char *p = strpbrk(line, "\n\r");
			if (p) *p = '\0'; // terminate the string at the newline
			if (sscanf(line, "%d=%n", &iValue, &iLen) == 1) {
				assert(iValue <= NUM_MIDI_PERC);
				snprintf(cPercName[iValue], INSTR_NAMELEN, "%s [%d]", &line[iLen], iValue);
			} else if ((line[0] != '#') && (line[0] != '\n')) {
				fprintf(stderr, "Invalid line in " PERC_NAME_FILE ": %s\n", line);
			}
		}
		fclose(p);
	}
	
	FILE* f = fopen(MAPPING_FILE, "r");
  if (!f) {
		fprintf(stderr, "Warning: Unable to open instrument mapping file " MAPPING_FILE ", defaulting to a Grand Piano\nfor all instruments.\n");
		return true;
	}
	INSTRUMENT in;
  memset(&in, 0, sizeof(in));
	in.redirect = -1; // none of these should redirect (but later automatic instruments will redirect to these ones)
  //strcpy(in.name, filename);
//  if (strncasecmp(filename, "DRUM", 4) == 0)
//    in.isdrum = 1;
	// Loop until we run out of lines in the data file or we hit the maximum number of instruments
	char value[256];
	for (::instrcnt = 0; fgets(line, sizeof(line)-1, f) && (instrcnt < MAXINSTR);) {
//  while (fgets(line, sizeof(line)-1, f))
//  {
//    int code, param1, param2;
		if (sscanf(line, "%02X-%02X/%02X-%02X/%02X-%02X/%02X-%02X/%02X/%02X-%02X: %s\n", &in.reg20[0], &in.reg20[1],
		 	&in.reg40[0], &in.reg40[1],
			&in.reg60[0], &in.reg60[1],
			&in.reg80[0], &in.reg80[1],
			&in.regC0,
			&in.regE0[0], &in.regE0[1], &value) == 12)
		{
			int iValue;
			if (sscanf(value, "patch=%d", &iValue) == 1) {
				// MIDI patch
				in.isdrum = 0;
				in.prog = iValue - 1;
				if ((in.prog < 0) || (in.prog > 127)) {
					fprintf(stderr, "ERROR: Instrument #%d was set to patch=%d, but this value must be between 1 and 128 inclusive.\n", instrcnt, in.prog+1);
					return false;
				}
				sprintf(in.name, "Instrument #%d: %s", instrcnt, cPatchName[in.prog]);
			} else if (sscanf(value, "drum=%d", &iValue) == 1) {
				// MIDI drum
				in.isdrum = 1;
				in.prog = -1;
				in.note = iValue;
				if ((in.note < 0) || (in.note > 127)) {
					fprintf(stderr, "ERROR: Instrument #%d (perc) was set to drum=%d, but this value must be between 1 and 128 inclusive.\n", instrcnt, in.note);
					return false;
				}
				sprintf(in.name, "Instrument #%d (perc): %s", instrcnt, cPercName[in.note]);
			} else {
				fprintf(stderr, "Unknown option %s\n", value);
				return false;
			}

			// Add instrument
			::instr[instrcnt++] = in;

		} else if ((line[0] != '#') && (line[0] != '\r') && (line[0] != '\n')) {
			fprintf(stderr, "Unable to parse this line:\n\n%s\n", line);
			return false;
		} // else the line starts with a # so it's a comment, ignore it.

/*    if (sscanf(line, "%02X: %02X %02X", &code, &param1, &param2) == 3)
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
      fprintf(stderr, "invalid line: %s\n", line);*/
  }
  fclose(f);
  return true;
}

/*
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
/=*
	int done;
struct ffblk ff;

  done = findfirst("*.reg", &ff, 0);
  while (!done)
  {
    if (loadinstr(ff.ff_name, instr[instrcnt]))
      instrcnt++;
    done = findnext(&ff);
  }
*=/
}*/

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
	long bestdiff = -1;
	for (int i = 0; i < instrcnt; i++)
	{
		long diff = compareinstr(instr[i], reg[channel]);
		if (besti < 0 || diff < bestdiff)
		{
			bestdiff = diff;
			besti = i;
			if (bestdiff == 0) break;
		}
	}
//	if (instr[besti].redirect != -1) {
		// This instrument was an automatically generated one to avoid printing the instrument definition multiple times, so
		// instead of using the auto one, use the one it originally matched against.
	if (besti >= 0) { // could be -1 if no instruments are loaded
		while (instr[besti].redirect >= 0) {
			// Could have multiple redirects
			besti = instr[besti].redirect;
		}
	}
//	}
	if (bestdiff != 0) {
		if (::bPerfectMatchesOnly) {
			// User doesn't want "close enough is good enough" instrument guessing
			besti = 0;  // use first instrument
		}
		// Couldn't find an exact match, print the details
		printf("**| New instrument in use on channel %d - copy this into " MAPPING_FILE " to assign it a MIDI patch:\n", channel);
		printf("  |   %02X-%02X/%02X-%02X/%02X-%02X/%02X-%02X/%02X/%02X-%02X: patch=?\n", reg[channel].reg20[0], reg[channel].reg20[1],
			reg[channel].reg40[0], reg[channel].reg40[1],
			reg[channel].reg60[0], reg[channel].reg60[1],
			reg[channel].reg80[0], reg[channel].reg80[1],
			reg[channel].regC0,
			reg[channel].regE0[0], reg[channel].regE0[1]
		);
		printf("  | Using instrument #%d instead: %s\n\n", besti, instr[besti].name);
		// Save this unknown instrument as a known one, so the same registers don't get printed again
//		reg[channel].prog = instr[besti].prog;  // but keep the same patch that we've already assigned to the instrument, so it doesn't drop back to a piano for the rest of the song
		// Maybe ^ isn't necessary if we're redirecting?
		instr[instrcnt] = reg[channel];
		if (besti >= 0) {
			instr[instrcnt].redirect = besti;  // Next time this instrument is matched, use the original one instead
		} else {
			instr[instrcnt].redirect = -1;  // Will only happen when no instruments are loaded
		}
		instrcnt++;
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
			::bRhythm = false;
		} else if (strncasecmp(*argv, "-p", 2) == 0) {
			::bUsePitchBends = false;
		} else if (strncasecmp(*argv, "-a", 2) == 0) {
			::bApproximatePitchbends = true;
		} else if (strncasecmp(*argv, "-i", 2) == 0) {
			::bPerfectMatchesOnly = true;
		} else if (strncasecmp(*argv, "--version", 9) == 0) {
			version();
			return 0;
		} else {
			fprintf(stderr, "invalid option %s\n", *argv);
	    usage();
		}
    argc--; argv++;
  }
  if (argc < 2)
    usage();

	if ((::bUsePitchBends) && (::bApproximatePitchbends)) {
		fprintf(stderr, "ERROR: Pitchbends can only be approximated (-a) if proper MIDI pitchbends are disabled (-p)\n");
		return 1;
	}

  input = argv[0];
  output = argv[1];
  if (strcmp(input, output) == 0)
  {
    fprintf(stderr, "cannot convert to same file\n");
    return 1;
  }

	if (!loadInstruments()) return 1;


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
	iSpeed = 0;
	if (strncmp(cSig, "DBRAWOPL", 8) == 0) {
		::iFormat = FORMAT_DRO;
		printf("Input file is in DOSBox DRO format.\n");
		::iInitialSpeed = 1000;

		fseek(f, 16, SEEK_SET); // seek to "length in bytes" field
	  imflen = fgetc(f);
	  imflen += fgetc(f) << 8L;
	  imflen += fgetc(f) << 16L;
	  imflen += fgetc(f) << 24;
	} else if (strncmp(cSig, "RAWADATA", 8) == 0) {
		::iFormat = FORMAT_RAW;
		printf("Input file is in Rdos RAW format.\n");

		// Read until EOF (0xFFFF is really the end but we'll check that during conversion)
		fseek(f, 0, SEEK_END);
	  imflen = ftell(f);

		fseek(f, 8, SEEK_SET); // seek to "initial clock speed" field
		::iInitialSpeed = 1000;
		int iClockSpeed = fgetc(f) + (fgetc(f) << 8L);
		if ((iClockSpeed == 0) || (iClockSpeed == 0xFFFF)) {
			::iSpeed = (int)18.2; // default to 18.2Hz...well, 18Hz thanks to rounding
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
		  //imflen = cSig[0] + (cSig[1] << 8);  // doesn't seem to work, even if chars are forced unsigned?!  (try wolf3d/salute.imf)
			fseek(f, 0, SEEK_SET);
			imflen = fgetc(f) + (fgetc(f) << 8L);
			fseek(f, 2, SEEK_SET);
		}
		if (strcasecmp(&input[strlen(input)-3], "imf") == 0) {
			printf("File extension is .imf - using 560Hz speed (rename to .wlf if this is too slow)\n");
			::iInitialSpeed = 560;
		} else if (strcasecmp(&input[strlen(input)-3], "wlf") == 0) {
			printf("File extension is .wlf - using 700Hz speed (rename to .imf if this is too fast)\n");
			::iInitialSpeed = 700;
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
	if (iSpeed == 0) iSpeed == iInitialSpeed;
//	int iInitialSpeed = ::iSpeed;
	resolution = iInitialSpeed / 2;
  write->head(/* version */ 0, /* track count updated later */0, resolution);


  write->track();
  write->tempo((long)(60000000.0 / tempo));
  write->tact(4,4,24,8);

  for (c = 0; c  <= 8; c++)
    lastprog[c] = -1;

//  printf("guessing instruments:\n");
  for (c = 0; c <= 8; c++)
  {
    write->volume(c, 127);
//    write->program(c, c);
    lastprog[c] = -1;
		reg[c].iOctave = 0;
  }

  int delay = 0;
  int channel;
  int code, param;
	
  int octave = 0;
  int curfreq[9];
  bool keyAlreadyOn[9];
	int lastkey[9];
	int pitchbent[9];
	int drumnote[9]; // note to play on MIDI channel 10 if Adlib channel has a percussive instrument assigned to it

	
	int rhythm[5]; // are these rhythm instruments currently playing?
  for (c = 0; c < 5; c++) rhythm[c] = 0;

  for (c = 0; c < 9; c++)
  {
    curfreq[c] = 0;
    mapchannel[c] = c;  // This can get reset when playing a drum and then a normal instrument on a channel - see instrument-change code below
		keyAlreadyOn[c] = false;
		lastkey[c] = -1;
		pitchbent[c] = (int)pitchbend_center;
		drumnote[c] = 0; // probably not necessary...

		if (::bUsePitchBends) {
			write->control(mapchannel[c], 100, 0);  // RPN LSB for "Pitch Bend Sensitivity"
			write->control(mapchannel[c], 101, 0);  // RPN MSB for "Pitch Bend Sensitivity"
			write->control(mapchannel[c], 6, (int)PITCHBEND_RANGE); // Data for Pitch Bend Sensitivity (in semitones) - controller 38 can be used for cents in addition
			write->control(mapchannel[c], 100, 0x7F);  // RPN LSB for "Finished"
			write->control(mapchannel[c], 101, 0x7F);  // RPN MSB for "Finished"
		}
//		write->pitchbend(mapchannel[c], pitchbend_center);
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
					case 0x00: // delay
						delay += param;//fgetc(f);
						//imflen--;
						continue;
					case 0x02: // control data
						switch (param) {
							case 0x00: {
								if (delay != 0) {
									// See below - we need to write out any delay at the old clock speed before we change it
							    write->time((delay * iInitialSpeed / ::iSpeed));
									delay = 0;
								}
								int iClockSpeed = fgetc(f) + (fgetc(f) << 8L);
								if ((iClockSpeed == 0) || (iClockSpeed == 0xFFFF)) {
									printf("Speed set to invalid value, ignoring speed change.\n");
								} else {
									::iSpeed = (int)round(1193180.0 / iClockSpeed);
									printf("Speed changed to %dHz\n", iSpeed);
								}
								imflen -= 2;
								break;
							}
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
		    if (delay != 0) write->time((delay * iInitialSpeed / ::iSpeed));
				//printf("delay is %d (ticks %d)\n", (delay * iInitialSpeed / ::iSpeed), delay);
				delay = 0;
				break;

			default: // should never happen
				break;

		} // switch (::iFormat)

    if (code >= 0xa0 && code <= 0xa8) // set freq bits 0-7
    {
      channel = code-0xa0;
      curfreq[channel] = (curfreq[channel] & 0xF00) + (param & 0xff);
			if (keyAlreadyOn[channel]) {
				param = 0x20; // bare noteon for code below
				octave = reg[channel].iOctave;
				goto doNoteOn;
//	      double keyFrac = freq2key(curfreq[channel], octave);
//  	    int key = (int)round(keyFrac);
//				doFreqChange(channel, key, keyFrac);
			}
      continue;
    }
    else if (code >= 0xB0 && code <= 0xB8) // set freq bits 8-9 and octave and on/off
    {
      channel = code - 0xb0;
      curfreq[channel] = (curfreq[channel] & 0x0FF) + ((param & 0x03)<<8);
      octave = (param >> 2) & 7;
			reg[channel].iOctave = octave; // save in case rhythm instruments will be sounding on this channel (??? - Malv)
doNoteOn:  // yes I know, but it's easier this way
      int keyon = (param >> 5) & 1;


      //int key = freq2key(curfreq[channel], octave);
      double keyFrac = freq2key(curfreq[channel], octave);
      int key = (int)round(keyFrac);
			//printf("key: %lf\n", key);
      if ((key > 0) && (keyon)) {
				// This is set to true to forcibly stop a MIDI keyon being generated for this note.  This is done when
				// a pitchbend is deemed as having done the job properly.
				bool bKeyonAgain = true;

				if (keyAlreadyOn[channel]) {
					// There's already a note playing on this channel, just worry about the pitch of that

					if (mapchannel[channel] != gm_drumchannel) {
						// We're using a normal instrument here

						if (::bUsePitchBends) {
								// It's the same note, but the pitch is off just slightly, use a pitchbend
								//double dbDiff = fabs(keyFrac - key); // should be between -0.9999 and 0.9999
								double dbDiff = keyFrac - (double)(lastkey[channel]); // hopefully between -PITCHBEND_RANGE and PITCHBEND_RANGE
							
								if (dbDiff > PITCHBEND_RANGE) {
									fprintf(stderr, "Warning: This song wanted to pitchbend by %.2f notes, but the maximum is %.1f\n", dbDiff, PITCHBEND_RANGE);

									// Turn this note off
									write->noteoff(mapchannel[channel], lastkey[channel]);
									lastkey[channel] = 0;
									keyAlreadyOn[channel] = false;
									// leave bKeyonAgain as true, so that a noteon will be played instead
								} else {
									int iNewBend = (int)(pitchbend_center + (PITCHBEND_ONESEMITONE * dbDiff));
									if (iNewBend != pitchbent[channel]) {
										//printf("pitchbend to %d/%.2lf (center + %d) (%.2lf semitones)\n", iNewBend, (double)pitchbend_center*2, (int)(iNewBend - pitchbend_center), (double)dbDiff);
										write->pitchbend(mapchannel[channel], iNewBend); // pitchbends are between 0x0000L and 0x2000L
										pitchbent[channel] = iNewBend;
									}
									// This pitchbend has done the job, don't play a noteon
									bKeyonAgain = false;
								}
						} else {
							// We're not using pitchbends, so just switch off the note if it's different (the next one will play below)
							if ((::bApproximatePitchbends) && (key != lastkey[channel])) {
								write->noteoff(mapchannel[channel], lastkey[channel]);
								lastkey[channel] = 0;
								keyAlreadyOn[channel] = false;
								//bKeyonAgain = true;
							} else {
								// Same note, different pitch, just pretend like it's not there
								bKeyonAgain = false;
							}
						}
					} else {
						// This is a percussive note, so no pitchbends.  But we don't want to play the note again, 'cos it's
						// already on.
						bKeyonAgain = false;
					}
				} // else this is a percussive instrument

				//} else {
				//if ((!bDontKeyonAgain) && ((!keyAlreadyOn[channel]) || (::bUsePitchBends))) {  // If *now* there's no note playing... (or we're using pitchbends, i.e. a portamento has been set up)
				if (bKeyonAgain) {  // If *now* there's no note playing... (or we're using pitchbends, i.e. a portamento has been set up)
					// See if we need to update anything

					// See if the instrument needs to change
					int i = findinstr(channel);
					if (
						(i >= 0) && (
							(instr[i].prog != lastprog[channel]) ||
							(
								(instr[i].isdrum) &&
								(drumnote[channel] != instr[i].note)
							)
						)
					) {
						printf("  channel %d set to: %s\n", channel, instr[i].name);
						//if (instr[i].prog >= 0 && !instr[i].isdrum && mapchannel[channel] == channel)
						if (instr[i].prog >= 0 && !instr[i].isdrum) {
							if (mapchannel[channel] == gm_drumchannel) {
								// This was playing drums, now we're back to normal notes
								mapchannel[channel] = channel; // make sure this matches the init section above
								drumnote[channel] = -1;
							}
							write->program(mapchannel[channel], lastprog[channel] = instr[i].prog);
						} else {
							// This new instrument is a drum
							mapchannel[channel] = gm_drumchannel;
							lastprog[channel] = instr[i].prog;
							assert(instr[i].prog == -1);
							drumnote[channel] = instr[i].note;
						}
					}
					
					// Play the note
	    	  //if ((::bUsePitchBends) && (!keyAlreadyOn[channel])) {
	    	  if ((::bUsePitchBends) && (mapchannel[channel] != gm_drumchannel)) { // If pitchbends are enabled and this isn't a percussion instrument
						double dbDiff = keyFrac - key; // should be between -0.9999 and 0.9999
						assert(dbDiff < PITCHBEND_RANGE); // not really necessary...

						int iNewBend = (int)(pitchbend_center + (PITCHBEND_ONESEMITONE * dbDiff));
				    if (iNewBend != pitchbent[channel]) {
							//printf("new note at pitchbend %d\n", iNewBend);
							write->pitchbend(mapchannel[channel], iNewBend); // pitchbends are between 0x0000L and 0x2000L
							pitchbent[channel] = iNewBend;
						}
					}

					int level = reg[channel].reg40[0] & 0x3f;
					if (level > (reg[channel].reg40[1] & 0x3f))
					level = reg[channel].reg40[1] & 0x3f;
					if (mapchannel[channel] != gm_drumchannel) {
						// Normal note
						write->noteon(mapchannel[channel], key, (0x3f - level) << 1);
						lastkey[channel] = key;
					} else {
						// Percussion
						write->noteon(gm_drumchannel, drumnote[channel], (0x3f - level) << 1);
						lastkey[channel] = drumnote[channel];
					}

					keyAlreadyOn[channel] = true;
					
				}

      } else {
				write->noteoff(mapchannel[channel], lastkey[channel]);
				lastkey[channel] = 0;
				keyAlreadyOn[channel] = false;
				//pitchbent[channel] = pitchbend_center;
			}
    }
		else if ((code == 0xBD) && (::bRhythm))
		{
			
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

					rhythm[4] = key;
				} else if (rhythm[4]) {
					// Bass drum off
					int channel = CHAN_BASSDRUM;
					//write->noteoff(gm_drumchannel, KEY_BASSDRUM);
					write->noteoff(channel, rhythm[4]);
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
						printf("channel %d: Rhythm-mode tom tom\n", channel);
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
					rhythm[2] = key;
				} else if (rhythm[2]) {
					// Tom tom off
					int channel = CHAN_TOMTOM;
					//write->noteoff(gm_drumchannel, KEY_TOMTOM);
					write->noteoff(channel, rhythm[2]);
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
//			if (rhythm[4]) { write->noteoff(gm_drumchannel, KEY_BASSDRUM);  rhythm[4] = 0; }
				if (rhythm[4]) { write->noteoff(CHAN_BASSDRUM,  rhythm[4]);     rhythm[4] = 0; }
				if (rhythm[3]) { write->noteoff(gm_drumchannel, KEY_SNAREDRUM); rhythm[3] = 0; }
//			if (rhythm[2]) { write->noteoff(gm_drumchannel, KEY_TOMTOM);    rhythm[2] = 0; }
				if (rhythm[2]) { write->noteoff(CHAN_TOMTOM,    rhythm[2]);     rhythm[2] = 0; }
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

	printf("\nConversion complete.  Wrote %s\n", output);
  return 0;
}
