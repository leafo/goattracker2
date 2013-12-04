/*
 * GoatTracker V2.0 beta songconvertor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "bme_end.h"
#include "gcommon.h"

#define MAX_SPLITS 16

INSTR instr[MAX_INSTR];
unsigned char ltable[3][MAX_TABLELEN*2];
unsigned char rtable[3][MAX_TABLELEN*2];
unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];
unsigned char pattern[MAX_PATT][MAX_PATTROWS*4+4];
unsigned char songname[MAX_STR];
unsigned char authorname[MAX_STR];
unsigned char copyrightname[MAX_STR];
int pattlen[MAX_PATT];
int songlen[MAX_SONGS][MAX_CHN];

int highestusedpattern = 0;
int highestusedinstr = 0;

int wavelen = 0, pulselen = 0, filtlen = 0;

int vibdepth = 0, pulse = 0;

int main(int argc, char **argv);
int loadsong(char *name);
int savesong(char *name);
void countpatternlengths(void);
void clearsong(void);
int gettablelen(int num);

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    printf("Usage: BETACONV <source> <destination> [vibdepth] [pulse]\n"
           "[vibdepth] decides whether to halve vibdepth (1=yes 0=no), default 0\n"
           "[pulse] decides whether to halve pulse speed (1=yes 0=no), default 0\n\n"
           "Converts GT2 early beta (47 instr.) song to GT2 current format (63 instr.)\n"
           "Optionally, halves vibrato depths and pulse speeds.\n");
    return 1;
  }

  if (argc >= 4)
  {
    sscanf(argv[3], "%u", &vibdepth);
  }

  if (argc >= 5)
  {
    sscanf(argv[4], "%u", &pulse);
  }

  if (!loadsong(argv[1]))
  {
    printf("ERROR: Couldn't load source song.");
    return 1;
  }
  if (!savesong(argv[2]))
  {
    printf("ERROR: Couldn't save destination song.");
    return 1;
  }
  return 0;
}

int loadsong(char *name)
{
  char ident[4];
  int c;

  FILE *srchandle = fopen(name, "rb");
  if (srchandle)
  {
    fread(ident, 4, 1, srchandle);
    if (!memcmp(ident, "GTS2", 4))
    {
      int d;
      unsigned char length;
      unsigned char amount;
      int loadbytes;
      clearsong();

      // Read infotexts
      fread(songname, sizeof songname, 1, srchandle);
      fread(authorname, sizeof authorname, 1, srchandle);
      fread(copyrightname, sizeof copyrightname, 1, srchandle);

      // Read songorderlists
      amount = fread8(srchandle);
      for (d = 0; d < amount; d++)
      {
        for (c = 0; c < MAX_CHN; c++)
        {
          length = fread8(srchandle);
          loadbytes = length;
          loadbytes++;
          fread(songorder[d][c], loadbytes, 1, srchandle);
        }
      }
      // Read instruments
      for (c = 1; c <= 47; c++)
      {
        instr[c].ad = fread8(srchandle);
        instr[c].sr = fread8(srchandle);
        instr[c].ptr[WTBL] = fread8(srchandle);
        instr[c].ptr[PTBL] = fread8(srchandle);
        instr[c].ptr[FTBL] = fread8(srchandle);
        instr[c].vibdelay = fread8(srchandle);
        instr[c].ptr[STBL] = fread8(srchandle);
        if (vibdepth)
          instr[c].ptr[STBL] = (instr[c].ptr[STBL] & 0xf0) | ((instr[c].ptr[STBL] & 0x0f) >> 1);
        instr[c].gatetimer = fread8(srchandle);
        instr[c].firstwave = fread8(srchandle);
        fread(&instr[c].name, MAX_INSTRNAMELEN, 1, srchandle);
      }
      // Read tables
      for (c = 0; c < 3; c++)
      {
        loadbytes = fread8(srchandle);
        fread(ltable[c], loadbytes, 1, srchandle);
        fread(rtable[c], loadbytes, 1, srchandle);
        if ((c == PTBL) && (pulse))
        {
          for (d = 0; d < loadbytes; d++)
          {
            if ((ltable[PTBL][d]) && (ltable[PTBL][d] < 0x80))
            {
              // Halve positive pulsespeed
              if (rtable[PTBL][d] < 0x80)
              {
                rtable[PTBL][d] >>= 1;
              }
              else
              {
                rtable[PTBL][d] >>= 1;
                rtable[PTBL][d] |= 0x80;
              }
            }
          }
        }
      }
      // Read patterns
      amount = fread8(srchandle);
      for (c = 0; c < amount; c++)
      {
        length = fread8(srchandle);
        fread(pattern[c], length*4, 1, srchandle);
        for (d = 0; d < length; d++)
        {
          // Convert notes in patterns to new format
          if (pattern[c][d*4] == 0xb0) pattern[c][d*4] = REST;
          if ((pattern[c][d*4] >= 0x50) && (pattern[c][d*4] <= 0xaf))
          {
            pattern[c][d*4] += 0x10;
            if (pattern[c][d*4] > REST) pattern[c][d*4] = REST;
          }

          // Halve vibrato depth
          if ((vibdepth) && (pattern[c][d*4+2] == 0x4))
            pattern[c][d*4+3] = (pattern[c][d*4+3] & 0xf0) | ((pattern[c][d*4+3] & 0x0f) >> 1);
        }
      }
      countpatternlengths();
      for (c = 1; c < MAX_INSTR; c++)
      {
        if ((instr[c].ad) || (instr[c].sr) || (instr[c].ptr[0]) || (instr[c].ptr[1]) ||
            (instr[c].ptr[2]) || (instr[c].vibdelay) || (instr[c].ptr[STBL]))
        {
          if (c > highestusedinstr) highestusedinstr = c;
        }
      }
      fclose(srchandle);
      return 1;
    }
    fclose(srchandle);
  }
  return 0;
}

int savesong(char *name)
{
  char ident[] = {'G', 'T', 'S', '2'};
  FILE *handle;
  int c;

  handle = fopen(name, "wb");
  if (handle)
  {
    int d;
    unsigned char length;
    unsigned char amount;
    int writebytes;
    fwrite(ident, 4, 1, handle);

    // Write infotexts
    fwrite(songname, sizeof songname, 1, handle);
    fwrite(authorname, sizeof authorname, 1, handle);
    fwrite(copyrightname, sizeof copyrightname, 1, handle);

    // Determine amount of songs to be saved
    c = 0;
    for (;;)
    {
      if (c == MAX_SONGS) break;
      if ((!songlen[c][0])||
        (!songlen[c][1])||
        (!songlen[c][2])) break;
      c++;
    }
    amount = c;

    fwrite8(handle, amount);
    // Write songorderlists
    for (d = 0; d < amount; d++)
    {
      for (c = 0; c < MAX_CHN; c++)
      {
        length = songlen[d][c]+1;
        fwrite8(handle, length);
        writebytes = length;
        writebytes++;
        fwrite(songorder[d][c], writebytes, 1, handle);
      }
    }
    // Write instruments
    fwrite8(handle, highestusedinstr);
    for (c = 1; c <= highestusedinstr; c++)
    {
      fwrite8(handle, instr[c].ad);
      fwrite8(handle, instr[c].sr);
      fwrite8(handle, instr[c].ptr[WTBL]);
      fwrite8(handle, instr[c].ptr[PTBL]);
      fwrite8(handle, instr[c].ptr[FTBL]);
      fwrite8(handle, instr[c].vibdelay);
      fwrite8(handle, instr[c].ptr[STBL]);
      fwrite8(handle, instr[c].gatetimer);
      fwrite8(handle, instr[c].firstwave);
      fwrite(&instr[c].name, MAX_INSTRNAMELEN, 1, handle);
    }
    // Write tables
    for (c = 0; c < 3; c++)
    {
      writebytes = gettablelen(c);
      fwrite8(handle, writebytes);
      fwrite(ltable[c], writebytes, 1, handle);
      fwrite(rtable[c], writebytes, 1, handle);
    }
    // Write patterns
    amount = highestusedpattern + 1;
    fwrite8(handle, amount);
    for (c = 0; c < amount; c++)
    {
      length = pattlen[c]+1;
      fwrite8(handle, length);
      fwrite(pattern[c], length*4, 1, handle);
    }

    fclose(handle);
    return 1;
  }
  return 0;
}

void countpatternlengths(void)
{
  int c, d, e;

  highestusedpattern = 0;
  highestusedinstr = 0;
  for (c = 0; c < MAX_PATT; c++)
  {
    for (d = 0; d <= MAX_PATTROWS; d++)
    {
      if (pattern[c][d*4] == ENDPATT) break;
      if ((pattern[c][d*4] != REST) || (pattern[c][d*4+1]) || (pattern[c][d*4+2]) || (pattern[c][d*4+3]))
        highestusedpattern = c;
      if (pattern[c][d*4+1] > highestusedinstr) highestusedinstr = pattern[c][d*4+1];
    }
    pattlen[c] = d;
  }
  for (e = 0; e < MAX_SONGS; e++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      for (d = 0; d < MAX_SONGLEN; d++)
      {
        if (songorder[e][c][d] >= LOOPSONG) break;
        if (songorder[e][c][d] < MAX_PATT)
        {
          if (songorder[e][c][d] > highestusedpattern)
            highestusedpattern = songorder[e][c][d];
        }
      }
      songlen[e][c] = d;
    }
  }
}

void clearsong(void)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    int d;
    for (d = 0; d < MAX_SONGS; d++)
    {
      memset(&songorder[d][c][0], 0, MAX_SONGLEN);
      if (!d)
      {
        songorder[d][c][0] = c;
        songorder[d][c][1] = LOOPSONG;
        songorder[d][c][2] = 0;
      }
      else
      {
        songorder[d][c][0] = LOOPSONG;
        songorder[d][c][1] = 0;
      }
    }
  }
  memset(songname, 0, sizeof songname);
  memset(authorname, 0, sizeof authorname);
  memset(copyrightname, 0, sizeof copyrightname);

  for (c = 0; c < MAX_PATT; c++)
  {
    int d;
    memset(&pattern[c][0], 0, MAX_PATTROWS*4);
    for (d = 0; d < MAX_PATTROWS; d++) pattern[c][d*4] = REST;
    for (d = MAX_PATTROWS; d <= MAX_PATTROWS; d++) pattern[c][d*4] = ENDPATT;
  }
  for (c = 0; c < MAX_INSTR; c++)
  {
    memset(&instr[c], 0, sizeof(INSTR));
  }
  memset(ltable, 0, sizeof ltable);
  memset(rtable, 0, sizeof rtable);
  countpatternlengths();
}

int gettablelen(int num)
{
  int c;

  for (c = MAX_TABLELEN-1; c >= 0; c--)
  {
    if (ltable[num][c] | rtable[num][c]) break;
   }
   return c+1;
}

