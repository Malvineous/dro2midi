#ifndef __SB_H__
#define __SB_H__
#include "sndcard.hpp"

class Soundblaster : public Soundcard
{
public:
  Soundblaster(int baseport = 220, int irqno = 5, int dma = 0);
  virtual ~Soundblaster();

  static Soundcard* recognize();  // if it is compatible then create soundcard

  virtual int reset();
  virtual void startinput();
  virtual void stopinput();

  virtual int hear(unsigned char* buf, int maxlen);
  virtual int play(unsigned char* buf, int len);

protected:

  int baseport_, irqno_, dma_;

  virtual int getbyte(); // -1 if not available
  virtual int putbyte(unsigned char c); // 0 if error
};

#endif
