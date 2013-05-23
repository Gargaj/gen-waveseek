#include <windows.h>
#include <tchar.h>
#include <Shlwapi.h>
#include <winamp/IN2.H>
#include <winamp/OUT.H>
#include <winamp/gen.h>
#include <winamp/wa_ipc.h>

#include "gen_waveseek.h"

void DummyOutConfig(HWND hwnd)
{
}

void DummyOutAbout(HWND hwnd)
{
}

void DummyOutInit()
{
}

void DummyOutQuit()
{
}

int nDummyOutWrittenTime = 0;
unsigned short pSampleBuffer[SAMPLE_BUFFER_SIZE];

unsigned int nFramePerWindow = 0;
unsigned int nBufferPointer = 0;

unsigned int nSampleRate = 0;
unsigned int nNumChannels = 0;
unsigned int nBitsPerSample = 0;
unsigned long nCurrentAmplitude = 0;
unsigned long nCurrentSampleCount = 0;
extern int nLengthInMS;

int DummyOutOpen(int samplerate, int numchannels, int bitspersamp, int bufferlenms, int prebufferms)
{
  nSampleRate = samplerate;
  nNumChannels = numchannels;
  nBitsPerSample = bitspersamp;
  nBufferPointer = 0;
  ZeroMemory(pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short));

  nCurrentSampleCount = 0;
  nCurrentAmplitude = 0;

  if(nLengthInMS > 0)
  {
    nFramePerWindow = MulDiv( nLengthInMS, nSampleRate, SAMPLE_BUFFER_SIZE * 1000) + 1;
  }

  return 1;
}

void DummyOutClose()
{
}

int DummyOutWrite(char *buf, int len)
{
  if (nFramePerWindow == 0)
    return 1;

  if (nBitsPerSample == 16)
  {
    short * p = (short *)buf;
    for (int i=0; i<len / 2; i++)
    {
      unsigned int nSample = abs(*(p++));
      nCurrentAmplitude = max( nCurrentAmplitude, nSample );
      nCurrentSampleCount++;

      if (nCurrentSampleCount / nNumChannels == nFramePerWindow && nBufferPointer < SAMPLE_BUFFER_SIZE)
      {
//         WCHAR sz[64];
//         _snwprintf(sz,64,L"sample: %6d - %8d samples - %.2f seconds\n",nBufferPointer,nDummyOutWrittenTime,nDummyOutWrittenTime/44100.0f/2.0/2.0);
//         OutputDebugStringW(sz);

        pSampleBuffer[nBufferPointer++] = nCurrentAmplitude; // *2 -> abs()
        nCurrentSampleCount = 0;
        nCurrentAmplitude = 0;
      }
    }
  }
  else
  {
    DebugBreak(); // todo?
  }
  
  nDummyOutWrittenTime += len;

  return 0;
}

int DummyOutCanWrite()
{
  return 64 * 1024;
}

int DummyOutIsPlaying()
{
  return 0;
}

int DummyOutPause(int pause)
{
  return 0;
}

void DummyOutSetVolume(int volume)
{
}

void DummyOutSetPanning(int pan)
{
}

void DummyOutFlush(int t)
{
}

int DummyOutGetOutputTime()
{
  return 0;
}

int DummyOutGetWrittenTime()
{
  return 0;
}

int DummyDSPIsActive()
{
  return 0;
}

int DummyDSPDoSamples(short int *samples, int numsamples, int bps, int nch, int srate)
{
  return numsamples;
}

Out_Module pDummyOutputPluginDescription = {
  OUT_VER,
  "",
  0xDEAD1234,
  0, // hmainwindow
  0, // hdllinstance
  DummyOutConfig,
  DummyOutAbout,
  DummyOutInit,
  DummyOutQuit,
  DummyOutOpen,
  DummyOutClose,
  DummyOutWrite,
  DummyOutCanWrite,
  DummyOutIsPlaying,
  DummyOutPause,
  DummyOutSetVolume,
  DummyOutSetPanning,
  DummyOutFlush,
  DummyOutGetOutputTime,
  DummyOutGetWrittenTime
};
