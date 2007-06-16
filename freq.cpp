#include "freq.hpp"

int freqlist[] =
{
  0x159, // C      24
  0x16B, // C#     25
  0x181, // D      26
  0x198, // D#     27
  0x1B0, // E      28
  0x1CA, // F      29
  0x1E5, // F#     30
  0x202, // G      31
  0x220, // G#     32
  0x241, // A      33
  0x263, // A#     34
  0x287, // B      35
  0x2AE, // C'     36
  0x2DB, // C#'    37
  0x306, // D'     38
  0x334, // D#'    39
  0x365, // E'	   40
  0x399, // F'	   41
  0x3CF, // F#'    42
  0x3FE, // G'     43
  0
};

#define DIST(a, b)   ((a > b) ? (a - b) : (b - a))

int besti = -1;

int searchfreq(int freq)
{
  besti = -1;
  int bestdist = 0;
  for (int i = 0; freqlist[i]; i++)
  {
    int dist = DIST(freq, freqlist[i]);
    if (besti < 0 || dist < bestdist)
    {
      besti = i;
      bestdist = dist;
    }
  }
  return besti >= 0;
}

int freq2key(int freq, int octave)
{
  octave *= 12;
  if (freq == 0)
    return 0;
  if (searchfreq(freq))
    return octave + besti;
  else
    return 0;
}

int nearestfreq(int freq)
{
  if (freq == 0)
    return -1;

  if (searchfreq(freq))
    return freqlist[besti];
  else
    return -1;
}

int relfreq(int freq, int halfnotes)
{
  if (!searchfreq(freq))
    return -1;
  int dir = (halfnotes > 0) ? +1 : -1;
	int i;
  for (i = besti; i >= 0 && freqlist[i] && halfnotes != 0; halfnotes -= dir, i += dir)
    ;
  if (halfnotes == 0)
    return freqlist[i];
  return -1;
}

int key2freq(int key, int &freq, int &octave)
{
  octave = key / 12 - 3;
  freq = freqlist[key % 12];

  return 1;
}
