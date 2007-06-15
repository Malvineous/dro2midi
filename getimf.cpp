#include <stdio.h>
#include <string.h>

char* head = "audiohed.bs1";
char* data = "audiot.bs1";

int nexti = 1;

void save(FILE* f, long pos, long len)
{
char name[14];
long imflen;

  if (len < 4)
    return;
  fseek(f, pos, SEEK_SET);
  fread(&imflen, 4, 1, f);
  if (imflen+4 > len)
    return;
  fseek(f, imflen, SEEK_CUR);
  while (fgetc(f) > 0)
    ;
  if (fread(name, 4, 1, f) != 1)
    return;
  if (strncmp(name, "IMF", 4) != 0)
    return;
  FILE* o;
  do
  {
    sprintf(name, "%d.imf", nexti);
    o = fopen(name, "r");
    if (o)
    {
      fclose(o);
      nexti++;
    }
    else
      break;
  } while (1);
  printf("%s: ofs=%08lX  length=%ld\n", name, pos, len);
  nexti++;
  o = fopen(name, "wb");
  if (!o)
  {
    perror(name);
    return;
  }
  fseek(f, pos, SEEK_SET);
  while (len-- > 0)
    fputc(fgetc(f), o);
  fclose(o);
}

void main(int argc, char** argv)
{
  argc--; argv++;
  if (argc == 0)
  {
    printf("usage: getimf [-head filename] [-data filename]\n");
    return;
  }
  while (argc > 0 && **argv == '-')
  {
    if (strcmp(*argv, "-head") == 0)
    {
      argc--; argv++;
      head = *argv;
      argc--; argv++; continue;
    }
    if (strcmp(*argv, "-data") == 0)
    {
      argc--; argv++;
      data = *argv;
      argc--; argv++; continue;
    }
    fprintf(stderr, "invalid option %s\n", *argv);
    argc--; argv++;
  }

  FILE* headf = fopen(head, "rb");
  if (!headf)
  {
    perror(head);
    return;
  }
  FILE* dataf = fopen(data, "rb");
  if (!dataf)
  {
    perror(data);
    return;
  }
  long pos = 0, lastpos = -1;
  while (fread(&pos, 4, 1, headf) == 1)
  {
    if (lastpos != -1 && lastpos < pos)
    {
      save(dataf, lastpos, pos-lastpos);
    }
    lastpos = pos;
  }
  fclose(headf);
  fclose(dataf);
}