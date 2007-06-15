// This Program needs a soundblaster compatible external midi interface
// and an external midi device (e.g. midi keyboard)
#include "adlib.hpp"
#include "freq.hpp"
#include "sb.hpp"
#include <stdio.h>
#include <conio.h>
#include <ctype.h>
#include <string.h>

Soundcard* card = 0;

unsigned char note = 0, vel = 0;
int pitch = 0x2000;

Soundcard* detect_soundcard()
{
  Soundcard* card;

  card = Soundblaster::recognize();
  if (card)
    return card;
  return 0;
}

int paramcnt(int c)
{
  switch(c & 0xF0)
  {
  case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xe0:
    return 2;
  case 0xC0: case 0xD0:
    return 1;
  }
  return 0;
}

void playnote()
{

  int freq, octave;
  if (key2freq(note, freq, octave) && octave >= 0 && octave <= 7)
  {
    int keyon = (vel > 0) ? 0x20 : 0;

    if (pitch < 0x2000)
      freq -= ((0x2000-pitch) >> 8);
    else if (pitch > 0x2000)
      freq += (pitch - 0x2000)>>8;
    ALsend(0xA0, freq & 0xff);
    ALsend(0xB0, (freq >> 8) +  (octave << 2) + keyon);
    if (!keyon)
    {
      vel = 0;
      note = 0;
    }
  }
}

void loadreg(char* filename)
{
char line[128];
  FILE* f = fopen(filename, "r");
  if (!f)
  {
    perror(filename);
    return;
  }

  while (fgets(line, sizeof(line)-1, f))
  {
    int code, param1, param2;

    if (sscanf(line, "%02X: %02X %02X", &code, &param1, &param2) == 3)
    {
      ALsend(code, param1);
      ALsend(code+3, param2);
    }
    else if (sscanf(line, "%02X: %02X", &code, &param1)  == 2)
    {
      ALsend(code, param1);
    }
    else if (strnicmp(line, "program:", 8) == 0)
      fprintf(stderr, "%s", line);
    else
      fprintf(stderr, "invalid line: %s\n", line);
  }
/*  // instrument
  ALsend(0x20, 0x11); ALsend(0x23, 0x54);
  ALsend(0x40, 0x43); ALsend(0x43, 0x45);
  ALsend(0x60, 0xF1); ALsend(0x63, 0xF0);
  ALsend(0x80, 0xFF); ALsend(0x83, 0xFF);
  ALsend(0xC0, 0x08);
  ALsend(0xE0, 0x00); ALsend(0xE3, 0x03);
*/
  fclose(f);
}

void main(int argc, char** argv)
{
  argc--; argv++;


  card = detect_soundcard();
  if (!card)
  {
    fprintf(stderr, "Could not detect soundcard\n");
    return;
  }
  ALreset();
  ALsend(0xBD, 0);
  ALsend(0x08, 0);

  if (argc > 0)
    loadreg(*argv);

  card->reset();
  card->startinput();
  unsigned char c;

  while (!kbhit() || getch() != 27)
  {
    if (!card->hear(&c, 1))
      continue;
    if (c == 0xf8)
      continue;
    if (c >= 0x80 && c <= 0x9F)
    {
      while (!card->hear(&note, 1))
	;
      while (!card->hear(&vel, 1))
	;

      if (c < 0x90)
	vel = 0;
      playnote();

//      printf("%02X %02X %02X\n", c, note, vel);
    }
    else if (c >= 0xE0 && c <= 0xEF)
    {
    unsigned char hi, lo;

      while (!card->hear(&lo, 1))
	;
      while (!card->hear(&hi, 1))
	;
      pitch = (int(hi & 0x7f) << 7) + (lo & 0x7f);
//      printf("pitch %04X\n", pitch);

      playnote();
    }
    else
    {
      int n = paramcnt(c);
      for (int i = 0; i<  n; i++)
      {
	while (!card->hear(&c, 1))
	  ;
      }
    }
  }

  delete card;
}
