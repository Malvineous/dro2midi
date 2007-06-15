#include "sb.hpp"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MIDI_READ_POLL	    0x30
#define MIDI_WRITE_POLL     0x38

int sb_environ(int &portno, int& irqno, int &dma)
{
char* env, *p;

  if (!(env = getenv("BLASTER")))
    return 0;
  p = strchr(env, 'A');
  if (p)
    sscanf(p+1, "%x", &portno);
  p = strchr(env, 'I');
  if (p)
    sscanf(p+1, "%x", &irqno);
  p = strchr(env, 'D');
  if (p)
    sscanf(p+1, "%x", &dma);
  return 1;
}

Soundblaster::Soundblaster(int baseport, int irqno, int dma)
{
  baseport_ = baseport;
  irqno_ = irqno;
  dma_ = dma;
}

Soundblaster::~Soundblaster()
{}


Soundcard* Soundblaster::recognize()
{
int baseport = 220, irqno = 5, dma = 0;

  if (sb_environ(baseport, irqno, dma))
  {
    Soundblaster* sb = new Soundblaster(baseport, irqno, dma);
    if (sb)
      return sb;
  }
  return 0; // not recognized
}

int Soundblaster::reset()
{
int port = baseport_ + 6;

  asm mov dx, port
  asm mov al, 1
  asm out dx,al
  asm sub al,al
wait:
  asm dec al
  asm jnz wait
  asm out dx, al

  int acknowledge = getbyte();
  // this byte should be AA if soundcard is installed
  return acknowledge == 0xAA;
}

void Soundblaster::startinput()
{
  putbyte(MIDI_READ_POLL);
}

void Soundblaster::stopinput()
{
  reset();
}

int Soundblaster::getbyte()
{
int port = baseport_;
int ret;

  asm push dx
  asm push cx
  asm mov dx, port
  asm add dl,0eh
  asm mov al, 0f0h
  asm mov cx,200h
retry:
  asm in al, dx
  asm or al,al
  asm js ready
  asm loop retry
  ret = -1;
  goto end;
ready:
  asm sub dl,4
  asm in al,dx
  ret = _AL;
end:
  asm pop cx
  asm pop dx
  return ret;
}

int Soundblaster::putbyte(unsigned char c)
{
int port = baseport_;
int ret;

  asm push cx
  asm push ax
  asm push dx
  asm mov dx, port
  asm add dx, 0ch
  asm mov al, c
  asm mov ah, al
  asm mov al, 0f0h
  asm mov cx,1000h
retry:
  asm dec cx
  asm jz timeout
  asm in al, dx
  asm or al,al
  asm js retry
  asm mov al, ah
  asm out dx,al
  ret = 1;
  goto end;

timeout:
  ret = 0;
end:
  asm pop dx
  asm pop ax
  asm pop cx
  return ret;
}

int Soundblaster::hear(unsigned char* buf, int maxlen)
{
int n = 0;
int c;

  while ( n < maxlen)
  {
    c = getbyte();
    if (c == -1)
      break;
    *buf++ = c;
    n++;
  }
  return n;
}

int Soundblaster::play(unsigned char* buf, int len)
{
int n = 0;

  while (len-- > 0)
  {
    if (!putbyte(MIDI_WRITE_POLL))
      break;
    if (!putbyte(*buf++))
      break;
    n++;
  }
  return n;
}
