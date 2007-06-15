// imf2midi v1.0 written by Günter Nagler 1996 (gnagler@ihm.tu-graz.ac.at)
// imf is an ADLIB format used in games like Wolfenstein, Blakestone
// it uses *.reg ascii files that describe ADLIB sounds

#include "midiio.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "freq.hpp"
#include <dir.h>

#ifdef __MSDOS__
#define WRITE_BINARY  "wb"
#define READ_TEXT     "r"
#else
#define WRITE_BINARY  "w"
#define READ_TEXT     "r"
#endif

#define PITCHBEND

char* input = 0;
char* output = 0;

FILE* f = 0;  // input file
MidiWrite* write = 0;

int resolution = 384;
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
} INSTRUMENT;

INSTRUMENT reg[9]; // current registers of channel
int lastprog[9];

void usage()
{
  fprintf(stderr, "imf2midi converts imf format to general midi\n");
  fprintf(stderr, "usage: imf2midi filename.imf filename.mid\n");
  fprintf(stderr, "uses instrument definitions from *.reg\n");
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
  if (strnicmp(filename, "DRUM", 4) == 0)
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
    else if (strnicmp(line, "program:", 8) == 0)
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
int done;
struct ffblk ff;

  done = findfirst("*.reg", &ff, 0);
  while (!done)
  {
    if (loadinstr(ff.ff_name, instr[instrcnt]))
      instrcnt++;
    done = findnext(&ff);
  }
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
  return besti;
}

int main(int argc, char**argv)
{
int c;

  argc--; argv++;
  while (argc > 0 && **argv == '-')
  {
    fprintf(stderr, "invalid option %s\n", *argv);
    argc--; argv++;
    usage();
  }
  if (argc < 2)
    usage();

  input = argv[0];
  output = argv[1];
  if (strcmp(input, output) == 0)
  {
    fprintf(stderr, "cannot convert imf to same file\n");
    return 1;
  }

  f = fopen(input, READ_BINARY);
  if (!f)
  {
    perror(input);
    return 1;
  }
  long imflen = 0;

  imflen = fgetc(f);
  imflen += fgetc(f) << 8L;
  imflen += fgetc(f) << 16L;
  imflen += fgetc(f) << 24;

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
    write->program(c, c);
    lastprog[c] = c;
  }

  int delay = 0;
  int octave = 0;
  int curfreq[9];
  int channel;
  int code, param;

  for (c = 0; c < 9; c++)
  {
    curfreq[c] = 0;
    mapchannel[c] = c;
  }
  while (imflen >= 4)
  {
    imflen-=4;

    delay = fgetc(f); delay += (fgetc(f) << 8);
    code = fgetc(f);
    param = fgetc(f);

    write->time(delay);

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
      int keyon = (param >> 5) & 1;

      int key = freq2key(curfreq[channel], octave);
      if (key > 0)
      {
#ifdef PITCHBEND
	int nextfreq = nearestfreq(curfreq[channel]);
	if (nextfreq == curfreq[channel])
	  write->pitchbend(mapchannel[channel], pitchbend_center); // normal position
	else if (nextfreq > curfreq[channel])  // pitch up
	{
	  int freq = relfreq(nextfreq, +2); // two halfnotes above
	  if (freq >= 0)
	  {
	    // pitch relative
	    write->pitchbend(mapchannel[channel], (int)(pitchbend_center + 0x2000L * (curfreq[channel]-nextfreq) / (freq-nextfreq)));
	  }
	}
	else // pitch down
	{
	  int freq = relfreq(nextfreq, -2); // two halfnotes down
	  if (freq >= 0)
	  {
	    // pitch relative
	    write->pitchbend(mapchannel[channel], (int)(pitchbend_center-0x2000L * (nextfreq-curfreq[channel]) / (nextfreq-freq)));
	  }
	}
#endif
	if (keyon)
	{
	  int i = findinstr(channel);
	  if (i >= 0 && instr[i].prog != lastprog[channel])
	  {
	    printf("channel %d: %s\n", channel, instr[i].name);

	    if (instr[i].prog >= 0 && !instr[i].isdrum && mapchannel[channel] == channel)
	      write->program(mapchannel[channel], lastprog[channel] = instr[i].prog);
	    else
	    {
	      mapchannel[channel] = gm_drumchannel;
	      lastprog[channel] = instr[i].prog;
	    }
	  }

	  int level = reg[channel].reg40[0] & 0x3f;
	  if (level > (reg[channel].reg40[1] & 0x3f))
	    level = reg[channel].reg40[1] & 0x3f;
	  write->noteon(mapchannel[channel], key, (0x3f - level) << 1);
	}
	else
	  write->noteoff(mapchannel[channel], key);
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
