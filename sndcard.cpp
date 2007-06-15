#include "sndcard.hpp"

Soundcard::Soundcard()
{}

Soundcard::~Soundcard()
{}


Soundcard* Soundcard::recognize()
{
  return 0; // not recognized
}

int Soundcard::reset()
{
  return 1;
}

void Soundcard::startinput()
{}

void Soundcard::stopinput()
{}

int Soundcard::hear(unsigned char* buf, int maxlen)
{
  return 0; // no midi data available
}

int Soundcard::play(unsigned char* buf, int len)
{
  return 0;
}
