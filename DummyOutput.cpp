#include <windows.h>
#include <shlwapi.h>
#include <winamp/in2.h>
#include <winamp/out.h>
#include <winamp/gen.h>
#include <winamp/wa_ipc.h>
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
			unsigned int nSample = abs(*(p++));
			nCurrentAmplitude = max(nCurrentAmplitude, nSample);
			++nCurrentSampleCount;

			if ((nCurrentSampleCount / nNumChannels) == nFramePerWindow && (nBufferPointer < SAMPLE_BUFFER_SIZE))
			{
				pSampleBuffer[nBufferPointer++] = nCurrentAmplitude; // *2 -> abs()
				nCurrentSampleCount = 0;
				nCurrentAmplitude = 0;
			}
		}
	}
	/*else
	{
		DebugBreak(); // todo?
	}*/
  
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