/* MPEG Sound library

   (C) 1997 by Jung woo-jae */

// Mpegtoraw.cc
// Server which get mpeg format and put raw format.

#ifdef _MSC_VER
#pragma warning(disable : 4244)
#endif

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "mpegsound.h"
#include "mpegsound_locals.h"

#define MY_PI 3.14159265358979323846

#define getLength_accurate

Mpegtoraw::Mpegtoraw(Soundinputstream *loader,Soundplayer *player)
{
  forcetomonoflag=false;
  downfrequency=0;

  lastfrequency = -1;

  this->loader=loader;
  this->player=player;

  // Initialize in case something goes wrong.
  version = mpeg1;
  mode = fullstereo;
  frequency = frequency44100;
}

#ifndef WORDS_BIGENDIAN
#define _KEY 0
#else
#define _KEY 3
#endif

int Mpegtoraw::getbits(int bits)
{
  union
  {
    char store[4];
    int current;
  }u;
  int bi;

  if(!bits)return 0;

  u.current=0;
  bi=(bitindex&7);
  u.store[_KEY]=buffer[bitindex>>3]<<bi;
  bi=8-bi;
  bitindex+=bi;

  while(bits)
  {
    if(!bi)
    {
      u.store[_KEY]=buffer[bitindex>>3];
      bitindex+=8;
      bi=8;
    }

    if(bits>=bi)
    {
      u.current<<=bi;
      bits-=bi;
      bi=0;
    }
    else
    {
      u.current<<=bits;
      bi-=bits;
      bits=0;
    }
  }
  bitindex-=bi;

  return (u.current>>8);
}

void Mpegtoraw::setforcetomono(bool flag)
{
  forcetomonoflag=flag;
}

void Mpegtoraw::setdownfrequency(int value)
{
  downfrequency=0;
  if(value)downfrequency=1;
}

bool Mpegtoraw::getforcetomono()
{
  return forcetomonoflag;
}

int Mpegtoraw::getdownfrequency()
{
  return downfrequency;
}

int  Mpegtoraw::getpcmperframe()
{
  int s;

  s=32;
  if(layer==3)
  {
    s*=18;
    if(version==0)s*=2;
  }
  else
  {
    s*=SCALEBLOCK;
    if(layer==2)s*=3;
  }

  return s;
}

inline void Mpegtoraw::flushrawdata()
{
  player->putblock((char *)rawdata,rawdataoffset<<1);
  currentframe++;
  rawdataoffset=0;
};

// Convert mpeg to raw
// Mpeg headder class
void Mpegtoraw::initialize()
{
  static bool initialized=false;

//  register REAL *s1,*s2;
//  REAL *s3,*s4;
  //printf("initialise\n");

  scalefactor=SCALE;
  calcbufferoffset=15;
  currentcalcbuffer=0;

//  s1=calcbufferL[0];s2=calcbufferR[0];
//  s3=calcbufferL[1];s4=calcbufferR[1];
  for(int i=CALCBUFFERSIZE-1;i>=0;i--)
    calcbufferL[0][i]=calcbufferL[1][i]=
    calcbufferR[0][i]=calcbufferR[1][i]=0.0;

  if(!initialized)
  {
    for(int i=0;i<16;i++)hcos_64[i]=1.0/(2.0*cos(MY_PI*double(i*2+1)/64.0));
    for(int i=0;i< 8;i++)hcos_32[i]=1.0/(2.0*cos(MY_PI*double(i*2+1)/32.0));
    for(int i=0;i< 4;i++)hcos_16[i]=1.0/(2.0*cos(MY_PI*double(i*2+1)/16.0));
    for(int i=0;i< 2;i++)hcos_8 [i]=1.0/(2.0*cos(MY_PI*double(i*2+1)/ 8.0));
    hcos_4=1.0/(2.0*cos(MY_PI*1.0/4.0));
    initialized=true;
  }

  layer3initialize();

  currentframe=decodeframe=0;
  if(loadheader())
  {
    #ifdef getLength_accurate
    totalframe=(loader->getsize()+framesize-1)/framesize;	// Estimate totalframes to create frameoffset array - this value will be a lot bigger for VBR files
    #else
    if(!vbr) {
      totalframe=(loader->getsize()+framesize-1)/framesize;	// Doesn't take into consideration id3 headers
    }
    #endif
    loader->setposition(0);
  }
  else totalframe=0;

  frameoffsets.resize(totalframe);
  std::fill(frameoffsets.begin(), frameoffsets.end(), 0);

  // First frameoffset will be 0 and doesn't matter even if frame does not start there because sync() is called in load_header to scan forward to first sync bit

  #ifdef getLength_accurate
  int  size = loader->getsize();

  int i = 0;
  for (; i < totalframe && frameoffsets[i] < size; ++i) {
    if(!loadheader()) {
      break;
    }
    frameoffsets[i]=loader->getposition();
  }

  totalframe = i;
  if (geterrorcode()==SOUND_ERROR_FINISH) seterrorcode(SOUND_ERROR_OK);	// seeking to end of file and trying to load header will trigger SOUND_ERROR_FINISH, reset to continue playback; Any other error will prevent playback
  loader->setposition(0);
  #endif
  player->setsoundtype(outputstereo,16,
                       frequencies[version][frequency]>>downfrequency);
};

void Mpegtoraw::setframe(int framenumber)
{
  int pos=0;

  if(frameoffsets.empty())return;
  if(framenumber==0)pos=frameoffsets[0];
  else
  {
    if(framenumber>=totalframe)framenumber=totalframe-1;
    pos=frameoffsets[framenumber];
    if(pos==0)
    {
      int i;

      for(i=framenumber-1;i>0;i--)
	if(frameoffsets[i]!=0)break;

      loader->setposition(frameoffsets[i]);

      while(i<framenumber)
      {
	loadheader();
	i++;
	frameoffsets[i]=loader->getposition();
      }
      pos=frameoffsets[framenumber];
    }
  }

  clearbuffer();
  loader->setposition(pos);
  decodeframe=currentframe=framenumber;
}


int Mpegtoraw::gettotalframes(void)
{
  return totalframe;
 }


void Mpegtoraw::clearbuffer()
{
//%%  player->abort();
//%%  player->resetsoundtype();
}

bool Mpegtoraw::loadheader()
{
  register int c;
  int length;
  bool flag;

  sync();

// Synchronize
  flag=false;
  do
  {

    if((c=loader->getbytedirect())<0)break;

    if(c==0xff) {
      while(!flag)
      {
	if((c=loader->getbytedirect())<0)
	{
	  flag=true;
	  break;
	}
	if((c&0xf0)==0xf0)
	{
	  flag=true;
	  break;
	}
	else if(c!=0xff)break;
      }
    }
    else if (c=='I') { // possible ID3v2 tag
        char buf[10];
        int c2,c3;


        if((c2=loader->getbytedirect())<0) break;
        if (c2=='D') {
            if((c3=loader->getbytedirect())<0) break;
            if(c3=='3') {
                  // OK, found ID3v2 tag.
                if (!loader->_readbuffer(buf,7)) break;
                
                  // Compute number of bytes to skip
                length=((buf[3]&0x7f)<<21) + ((buf[4]&0x7f)<<14) +
                    ((buf[5]&0x7f)<<7) + (buf[6]&0x7f);
                
       //printf("Found ID3v2 tag; skipping %d bytes\n",length);
                while (length-->0) {
                    if(loader->getbytedirect()<0) {
                        break;
                    }
                }
            }
        }
    }
    //else {
    //    printf("garbage character: %02x\n",c);
    //}
  }while(!flag);

  if(c<0) {
    seterrorcode(SOUND_ERROR_FINISH);
    return false;
  }



// Analyzing
  c&=0xf;
  protection=c&1;
  layer=4-((c>>1)&3);
  version=(_mpegversion)((c>>3)^1);

  c=((loader->getbytedirect()))>>1;
  padding=(c&1);
  c>>=1;
  frequency=(_frequency)(c&2); c>>=2;
  bitrateindex=(int)c;
  if(bitrateindex==15) {
    seterrorcode(SOUND_ERROR_BAD);
    return false;
  }

  c=((unsigned int)(loader->getbytedirect()))>>4;
  extendedmode=c&3;
  mode=(_mode)(c>>2);

// Check for VBR header - proof of concept code needs tidying
  #ifndef getLength_accurate
  int pos, i;

  pos = loader->getposition();
  char vbr_header[50];

  vbr = 0;

  loader->_readbuffer(vbr_header, 45);

  for (i=0; i<33; i++) {					// VBR identifying string "Xing" will be found in first 32 bytes of remaining header if mp3 is VBR
    if (vbr_header[i] == 'X') {
      if (vbr_header[i+1] == 'i') {
        if (vbr_header[i+2] == 'n') {
          if (vbr_header[i+3] == 'g') {
	    vbr = 1;
	     if (vbr_header[39] > 1) { 								// Means contains number of frames in header!
	       totalframe = ((vbr_header[40] << 24) & 0xFF000000);		// Need masks to prevent int -ve's causing incorrect calculation
	       totalframe += ((vbr_header[41] << 16) & 0x00FF0000);
	       totalframe += ((vbr_header[42] << 8) & 0x0000FF00);
	       totalframe += ((vbr_header[43]) & 0x000000FF);
	     }
	    break;
	  }
	}
      }
    }
  }



   loader->setposition(pos);		// bodge to get working seems to need to be in the position it was before vbr header for rest of code to function after vbr header check - no idea why - anyone!?
  #endif

 // Making information
  inputstereo= (mode==single)?0:1;
  if(forcetomonoflag)outputstereo=0; else outputstereo=inputstereo;

  /*  if(layer==2)
    if((bitrateindex>=1 && bitrateindex<=3) || (bitrateindex==5)) {
      if(inputstereo)return seterrorcode(SOUND_ERROR_BAD); }
    else if(bitrateindex==11 && mode==single)
    return seterrorcode(SOUND_ERROR_BAD); */

  channelbitrate=bitrateindex;
  if(inputstereo)
    if(channelbitrate==4)channelbitrate=1;
    else channelbitrate-=4;

  if(channelbitrate==1 || channelbitrate==2)tableindex=0; else tableindex=1;

  if(layer==1)subbandnumber=MAXSUBBAND;
  else
  {
    if(!tableindex)
      if(frequency==frequency32000)subbandnumber=12; else subbandnumber=8;
    else if(frequency==frequency48000||
	    (channelbitrate>=3 && channelbitrate<=5))
      subbandnumber=27;
    else subbandnumber=30;
  }

  if(mode==single)stereobound=0;
  else if(mode==joint)stereobound=(extendedmode+1)<<2;
  else stereobound=subbandnumber;

  if(stereobound>subbandnumber)stereobound=subbandnumber;

  // framesize & slots
  if(layer==1)
  {
    framesize=(12000*bitrate[version][0][bitrateindex])/
              frequencies[version][frequency];
    if(frequency==frequency44100 && padding)framesize++;
    framesize<<=2;
  }
  else
  {
    framesize=(144000*bitrate[version][layer-1][bitrateindex])/
      (frequencies[version][frequency]<<version);
    if(padding)framesize++;
    if(layer==3)
    {
      if(version)
	layer3slots=framesize-((mode==single)?9:17)
	                     -(protection?0:2)
	                     -4;
      else
	layer3slots=framesize-((mode==single)?17:32)
	                     -(protection?0:2)
	                     -4;
    }
  }

    // if frame size is invalid, fail
  if (framesize < 4 || framesize > 4100) {
    seterrorcode(SOUND_ERROR_BAD);
    return false;
  }

  if(!fillbuffer(framesize-4))seterrorcode(SOUND_ERROR_FILEREADFAIL);

  if(!protection)
  {
    getbyte();                      // CRC, Not check!!
    getbyte();
  }


  if(loader->eof()) {
    seterrorcode(SOUND_ERROR_FINISH);
    return false;
  }

  return true;
}



// Convert mpeg to raw
bool Mpegtoraw::run(int frames)
{
  clearrawdata();
  if(frames<0)lastfrequency=0;

  for(;frames;frames--)
  {
    if(totalframe>0)
    {
      if(decodeframe<totalframe)
	frameoffsets[decodeframe]=loader->getposition();
    }

    if(loader->eof())
    {
      seterrorcode(SOUND_ERROR_FINISH);
      break;
    }
    if(loadheader()==false)break;

    if(frequency!=lastfrequency)
    {
      if(lastfrequency>0)seterrorcode(SOUND_ERROR_BAD);
      lastfrequency=frequency;
    }
    if(frames<0)
    {
      frames=-frames;
      player->setsoundtype(outputstereo,16,
			   frequencies[version][frequency]>>downfrequency);
    }

    decodeframe++;

    if     (layer==3)extractlayer3();
    else if(layer==2)extractlayer2();
    else if(layer==1)extractlayer1();

    flushrawdata();
    if(player->geterrorcode())seterrorcode(geterrorcode());
  }

  return (geterrorcode()==SOUND_ERROR_OK);
}
