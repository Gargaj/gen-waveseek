#include <windows.h>
#include <stdlib.h>
#include <winamp/out.h>
#include <winamp/gen.h>
#include "gen_waveseek.h"

void DummyOutConfig(HWND /*hwnd*/) { }

void DummyOutAbout(HWND /*hwnd*/) { }

void DummyOutInit() { }

void DummyOutQuit() { }

unsigned short pSampleBuffer[SAMPLE_BUFFER_SIZE];

unsigned int nFramePerWindow = 0,
			 nBufferPointer = 0,
			 nNumChannels = 0,
			 nBitsPerSample = 0;
unsigned long nCurrentAmplitude = 0,
			  nCurrentSampleCount = 0;
extern int nLengthInMS;

int DummyOutOpen(int samplerate, int numchannels, int bitspersamp, int /*bufferlenms*/, int /*prebufferms*/)
{
	nNumChannels = numchannels;
	nBitsPerSample = bitspersamp;
	nCurrentSampleCount = nCurrentAmplitude = 0;

	if (nLengthInMS > 0)
	{
		nFramePerWindow = MulDiv(nLengthInMS, samplerate, SAMPLE_BUFFER_SIZE * 1000) + 1;
	}

	return 1;
}

void DummyOutClose() { }

void AddSample(unsigned int nSample)
{
	nCurrentAmplitude = max(nCurrentAmplitude, nSample);
	++nCurrentSampleCount;

	if (((nCurrentSampleCount / nNumChannels) == nFramePerWindow) && (nBufferPointer < SAMPLE_BUFFER_SIZE))
	{
		pSampleBuffer[nBufferPointer++] = nCurrentAmplitude;
		nCurrentSampleCount = 0;
		nCurrentAmplitude = 0;
	}
}

int DummyOutWrite(char *buf, int len)
{
	if (nFramePerWindow == 0)
	{
		return 1;
	}

	if (nBitsPerSample == 16)
	{
		const short * p = (short *)buf;
		for (int i = 0; i < len / 2; i++)
		{
			const unsigned int nSample = abs(*(p++));
			AddSample(nSample);
		}
	}
	else if (nBitsPerSample == 24)
	{
		const char * p = (char *)buf;
		for (int i = 0; i < len / 3; i++)
			{
			const unsigned int nSample = abs((((0xFF & *(p + 2)) << 24) | ((0xFF & *(p + 1)) << 16) | ((0xFF & *(p)) << 8)) >> 16);
			p += 3;
			AddSample((nSample));
			}
		}
	else
	{
		// if we don't support it then we need to flag it
		// so that a message is provided to the user else
		// it can cause confusion due to looking broken.
		extern bool bUnsupported;
		bUnsupported = true;
	}
  
	return 0;
}

int DummyOutCanWrite()
{
	return 65536;
}

int DummyOutIsPlaying()
{
	return 0;
}

int DummyOutPause(int /*pause*/)
{
	return 0;
}

void DummyOutSetVolume(int /*volume*/) { }

void DummyOutSetPanning(int /*pan*/) { }

void DummyOutFlush(int /*t*/) { }

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

int DummyDSPDoSamples(short int * /*samples*/, int numsamples, int /*bps*/, int /*nch*/, int /*srate*/)
{
	return numsamples;
}

Out_Module *pDummyOutput = NULL;

Out_Module *CreateOutput(HWND hwnd, HINSTANCE hDLL)
{
	DestroyOutput();

	nBufferPointer = 0;
	ZeroMemory(pSampleBuffer, SAMPLE_BUFFER_SIZE * sizeof(unsigned short));

	pDummyOutput = (Out_Module *)calloc(1, sizeof(Out_Module));
	if (pDummyOutput)
	{
		pDummyOutput->version = OUT_VER;
		pDummyOutput->id = 0xDEAD1234;
		pDummyOutput->hMainWindow = hwnd;
		pDummyOutput->hDllInstance = hDLL;
		pDummyOutput->Config = DummyOutConfig;
		pDummyOutput->About = DummyOutAbout;
		pDummyOutput->Init = DummyOutInit;
		pDummyOutput->Quit = DummyOutQuit;
		pDummyOutput->Open = DummyOutOpen;
		pDummyOutput->Close = DummyOutClose;
		pDummyOutput->Write = DummyOutWrite;
		pDummyOutput->CanWrite = DummyOutCanWrite;
		pDummyOutput->IsPlaying = DummyOutIsPlaying;
		pDummyOutput->Pause = DummyOutPause;
		pDummyOutput->SetVolume = DummyOutSetVolume;
		pDummyOutput->SetPan = DummyOutSetPanning;
		pDummyOutput->Flush = DummyOutFlush;
		pDummyOutput->GetOutputTime = DummyOutGetOutputTime;
		pDummyOutput->GetWrittenTime = DummyOutGetWrittenTime;
	}
	return pDummyOutput;
}

void DestroyOutput()
{
	if (pDummyOutput)
	{
		free(pDummyOutput);
		pDummyOutput = NULL;
	}
}