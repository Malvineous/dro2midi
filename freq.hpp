#include <math.h>

#ifdef _MSC_VER
// Keep MS VC++ happy
inline double log2(double a)
{
   return log(a) / log(2.0);
}
#endif

double freq2key(int freq, int octave);
/*

extern int freqlist[];

int freq2key(int freq, int octave);
int key2freq(int key, int &freq, int &octave);
int nearestfreq(int freq);
int relfreq(int freq, int halfnotes);
*/
