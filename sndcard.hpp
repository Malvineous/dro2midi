#ifndef __SNDCARD_HPP
#define __SNDCARD_HPP

class Soundcard
{
public:
  Soundcard();
  virtual ~Soundcard();

  static Soundcard* recognize();  // if it is compatible then create soundcard

  virtual int reset();
  virtual void startinput();
  virtual void stopinput();

  virtual int hear(unsigned char* buf, int maxlen);
  virtual int play(unsigned char* buf, int len);
};

#endif
