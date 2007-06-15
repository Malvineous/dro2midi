
extern int freqlist[];

int freq2key(int freq, int octave);
int key2freq(int key, int &freq, int &octave);
int nearestfreq(int freq);
int relfreq(int freq, int halfnotes);
