int DummyDSPIsActive();
int DummyDSPDoSamples(short int *samples, int numsamples, int bps, int nch, int srate);

#define SAMPLE_BUFFER_SIZE 4096

extern Out_Module pDummyOutputPluginDescription;
extern winampGeneralPurposePlugin pPluginDescription;
extern unsigned short pSampleBuffer[SAMPLE_BUFFER_SIZE];

extern unsigned int nFramePerWindow;
extern unsigned int nSampleRate;
extern unsigned int nNumChannels;
extern unsigned int nBitsPerSample;
extern unsigned int nBufferPointer;

