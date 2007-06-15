#include <dos.h>
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "adlib.hpp"

#define SHOWREGISTERS

typedef struct
{
  int reg20[2];
  int reg40[2];
  int reg60[2];
  int reg80[2];
  int regC0;
  int regE0[2];
} ADLIBREG;

ADLIBREG reg[9];  // store registers until first note playing
int playing[9];

int getchannel(int code)
{
int i;

/*
   Channel        0   1   2   3   4  5   6   7   8
   Operator 1    00  01  02  08  09  0A  10  11  12
   Operator 2    03  04  05  0B  0C  0D  13  14  15
*/
  if (code >= 0xC0 && code <= 0xC8)
    return code - 0xC0;
  if (code >= 0xA0 && code <= 0xA8)
   return code - 0xA0;
  if (code >= 0xB0 && code <= 0xB8)
   return code - 0xB0;
  if (code >= 0x20 && code <= 0x35)
    i = code-0x20;
  else if (code >= 0x40 && code <= 0x55)
    i = code-0x40;
  else if (code >= 0x60 && code <= 0x75)
    i = code-0x60;
  else if (code >= 0x80 && code <= 0x95)
    i = code-0x80;
  else if (code >= 0xE0 && code <= 0xF5)
    i = code-0xE0;
  else
  {
    return -1;
  }
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
  }
  return -1; // not valid
}

int op(int code)
{
  switch(code & 0x1F)
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
  }
  return -1;
}

void main(int argc, char** argv)
{
int d = 1;
int play[9];
int channel;

  memset(playing, 0, sizeof(playing));
  memset(reg, 0, sizeof(reg));

  for (int i = 0; i <= 8; i++)
    play[i] = 0;

  argc--; argv++;
  if (argc == 0)
    return;
  if (argc > 0 && **argv == '-' && isdigit((*argv)[1]))
  {
     d = atoi(*argv + 1);
     argc--; argv++;
  }
  while (argc > 0 && strcmp(*argv, "-c") == 0)
  {
    argc--; argv++;
    if (argc > 0)
    {
      channel = atoi(*argv);
      if (channel >= 0 && channel <= 8)
	play[channel] = 1;
      argc--; argv++;
    }
  }

  for (i = 0; i <= 8; i++)
  if (play[i])
    break;
  if (i == 9)
  {
    for (i = 0; i<= 8; i++)
      play[i] = 1;
  }

  FILE* f = fopen(*argv, "rb");
  if (!f)
    return;
  long len;
  fread(&len, 4, 1, f);

  ALreset();

  int c1, c2, c3;

  int count = 0;

  while (ftell(f) < len+4)
  {
    c3 = getw(f);
//    printf("%04X ", c3);
    count = c3;
    while (count-- > 0)
      delay(d);

    c1 = fgetc(f);
    c2 = fgetc(f);
    if (c1 < 0 || c2 < 0)
      break;
    channel = getchannel(c1);

    if (channel >= 0 && !playing[channel] && play[channel])
    {
       if (c1 >= 0x20 && c1 <= 0x35)
	 reg[channel].reg20[op(c1)] = c2;
       else if (c1 >= 0x40 && c1 <= 0x55)
	 reg[channel].reg40[op(c1)] = c2;
       else if (c1 >= 0x60 && c1 <= 0x75)
	 reg[channel].reg60[op(c1)] = c2;
       else if (c1 >= 0x80 && c1 <= 0x95)
	 reg[channel].reg80[op(c1)] = c2;
       else if (c1 >= 0xE0 && c1 <= 0xF5)
	 reg[channel].regE0[op(c1)] = c2;
       else if (c1 >= 0xC0 && c1 <= 0xC8)
	 reg[channel].regC0 = c2;
       else if (c1 >= 0xB0 && c1 <= 0xB8 && (c2 & 0x20) != 0)
       {
	 // print registers
#ifdef SHOWREGISTERS
	 fprintf(stderr, "channel %d:\n", channel);
	 printf("20: %02X %02X\n", reg[channel].reg20[0], reg[channel].reg20[1]);
	 printf("40: %02X %02X\n", reg[channel].reg40[0], reg[channel].reg40[1]);
	 printf("60: %02X %02X\n", reg[channel].reg60[0], reg[channel].reg60[1]);
	 printf("80: %02X %02X\n", reg[channel].reg80[0], reg[channel].reg80[1]);
	 printf("C0: %02X\n", reg[channel].regC0);
	 printf("E0: %02X %02X\n", reg[channel].regE0[0], reg[channel].regE0[1]);
#endif
	 playing[channel] = 1;
       }
    }
    if (channel < 0 || play[channel])
      ALsend(c1, c2);

    delay(d);
    if (kbhit() && getch() == 27)
      break;
  }
  ALreset();
}