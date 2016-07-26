#pragma once

int DummyDSPIsActive();
int DummyDSPDoSamples(short int *samples, int numsamples, int bps, int nch, int srate);

Out_Module* CreateOutput(HWND, HINSTANCE);
void DestroyOutput();

#define SAMPLE_BUFFER_SIZE 4096

extern winampGeneralPurposePlugin plugin;
extern unsigned short pSampleBuffer[SAMPLE_BUFFER_SIZE];