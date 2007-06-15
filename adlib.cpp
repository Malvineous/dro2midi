#include "adlib.hpp"
#include <dos.h>

void ALsend(int a, int b)
{
  outportb(0x388, a);

  for (int i = 0; i <= 6; i++)
    inportb(0x388);

  outportb(0x389, b);
  for (i = 0; i <= 6; i++)
    inportb(0x388);
}

void ALreset()
{
  for (int channel = 0; channel <= 8; channel++)
    ALsend(0xb0 + channel, 0);
}

