#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <winamp/in2.H>
#include <winamp/gen.h>
#include <winamp/wa_ipc.h>
#include <winamp/ipc_pe.h>
#include <gen_ml/ml.h>
#include <gen_ml/ml_ipc_0313.h>
#include <nu/AutoWide.h>
#include <nu/AutoChar.h>
#include <nu/AutoCharFn.h>
#include <nu/ServiceBuilder.h>
#include "resource.h"
#include "api.h"
#include "gen_waveseek.h"
#include "embedwnd.h"

#define PLUGIN_VERSION "2.3.0"

#ifdef GetPrivateProfileInt
#undef GetPrivateProfileInt
#define GetPrivateProfileInt(field,param) GetPrivateProfileIntW(INI_FILE_SECTION,field,param,ini_file)
#endif

#define WA_DLG_IMPLEMENT
#include <winamp/wa_dlg.h>
#include <strsafe.h>

#ifndef ID_PE_SCUP
#define ID_PE_SCUP   40289
#endif

#ifndef ID_PE_SCDOWN
#define ID_PE_SCDOWN 40290
#endif

// this is used to identify the skinned frame to allow for embedding/control by modern skins if needed
// {1C2F2C09-4F43-4CFF-9DE7-32E014638DFC}
static const GUID embed_guid = 
{ 0x1c2f2c09, 0x4f43, 0x4cff, { 0x9d, 0xe7, 0x32, 0xe0, 0x14, 0x63, 0x8d, 0xfc } };

HWND hWndWaveseek = NULL, hWndToolTip = NULL, hWndInner = NULL;
WNDPROC oldPlaylistWndProc = NULL;
embedWindowState embed = {0};
TOOLINFO ti = {0};
int on_click = 0, clickTrack = 1, showCuePoints = 0, hideTooltip = 0;
UINT WINAMP_WAVEFORM_SEEK_MENUID = 0xa1bb;

api_service* WASABI_API_SVC = NULL;
api_application *WASABI_API_APP = NULL;
api_language *WASABI_API_LNG = NULL;
// these two must be declared as they're used by the language api's
// when the system is comparing/loading the different resources
HINSTANCE WASABI_API_LNG_HINST = NULL, WASABI_API_ORIG_HINST = NULL;

void DummySAVSAInit(int maxlatency_in_ms, int srate) {}
void DummySAVSADeInit() {}
void DummySAAddPCMData(void *PCMData, int nch, int bps, int timestamp) {}
int DummySAGetMode() { return 0; }
int DummySAAdd(void *data, int timestamp, int csa) { return 0; }
void DummyVSAAddPCMData(void *PCMData, int nch, int bps, int timestamp) {}
int DummyVSAGetMode(int *specNch, int *waveNch) { return 0; }
int DummyVSAAdd(void *data, int timestamp) { return 0; }
void DummyVSASetInfo(int srate, int nch) {}
void DummySetInfo(int bitrate, int srate, int stereo, int synched) {}

WNDPROC lpWndProcOld = NULL, lpWndProc = NULL;

typedef In_Module *(*PluginGetter)();

#define TIMER_ID 31337
#define TIMER_FREQ 100

wchar_t *szDLLPath = 0, *ini_file = 0,
		szFilename[MAX_PATH] = {0},
		szWaveCacheDir[MAX_PATH] = {0},
		szWaveCacheFile[MAX_PATH] = {0},
		szTempDLLDestination[MAX_PATH] = {0},
		szUnavailable[128] = {0},
		szBadPlugin[128] = {0},
		szStreamsNotSupported[128] = {0},
		pluginTitleW[256] = {0};

In_Module * pModule = NULL;
int nLengthInMS = 0, no_uninstall = 1, delay_load = -1;
bool bIsCurrent = false, bIsLoaded = false, bIsProcessing = false;
int bUnsupported = 0;

DWORD delay_ipc = (DWORD)-1;

HFONT hFont = NULL;
HBRUSH hbrBackground = NULL;

COLORREF clrWaveform = RGB(0, 255, 0),
		 clrBackground = RGB(0, 0, 0),
		 clrCuePoint = RGB(117, 116, 139),
		 clrWaveformPlayed = RGB(0, 128, 0),
		 clrGeneratingText = RGB(0, 128, 0);

void PluginConfig();

void GetFilePaths()
{
	// find the winamp.ini for the Winamp install being used
	ini_file = (wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETINIFILEW);
	lstrcpyn(szWaveCacheDir, (wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETINIDIRECTORYW), ARRAYSIZE(szWaveCacheDir));

	// make the cache folder in the user's settings folder e.g. %APPDATA%\Winamp\Plugins\wavecache
	// which will better ensure that the cache will be correctly generated though it will fallback
	// to %PROGRAMFILES(x86)%\Winamp\Plugins\wavecache or %PROGRAMFILES%\Winamp\Plugins\wavecache
	// as applicable to the Windows and Winamp version being used (more so with pre v5.11 clients)
	PathAppend(szWaveCacheDir, L"Plugins\\wavecache");
	CreateDirectory(szWaveCacheDir, NULL);

	// find the correct Winamp\Plugins folder (using native api before making a good guess at it)
	szDLLPath = (wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORYW);
}

int GetFileInfo(const bool unicode, char* szFn, char szFile[MAX_PATH])
{
	wchar_t szTitle[GETFILEINFO_TITLE_LENGTH] = {0};
	int lengthInMS = -1;
	if (!unicode)
	{
		lstrcpynA(szFile, AutoCharFn(szFilename), MAX_PATH);
	}

	pModule->GetFileInfo((unicode ? (char*)szFn : szFile), (char*)szTitle, &lengthInMS);
	return lengthInMS;
}

void MPG123HotPatch(HINSTANCE module)
{
	struct
	{
		int offset;
		int change_len;
		int adjust;
		char code[9];
		char change[6];
	}
	blocks[] =
	{
		{0x7740, 2, 0, "\x75\x05\xE8\x89\xFD\xFF\xFF\x6A", "\xEB\x00"}, // 109.2 SSE2
		{0x76CB, 2, 0, "\x75\x05\xE8\x8E\xFD\xFF\xFF\x6A", "\xEB\x00"}, // 109.2 normal
		{0x7290, 2, 0, "\x75\x05\xE8\x89\xFE\xFF\xFF\x53", "\xEB\x00"},	// 112.1 SSE2
		{0x725B, 2, 0, "\x75\x05\xE8\x8E\xFE\xFF\xFF\x53", "\xEB\x00"}, // 112.1 normal
	};

	HANDLE curproc = GetCurrentProcess();
	for (int i = 0; i < ARRAYSIZE(blocks); i++)
	{
		// offset in gen_ml.dll when loaded
		char* p = (char*)((int)module + blocks[i].offset);

		if (!memcmp((LPCVOID)p, blocks[i].code, 8))
		{
			// nudge the start position for the code we want to patch as needed
			p += blocks[i].adjust;

			DWORD flOldProtect = 0, flDontCare = 0;
			if (VirtualProtect((LPVOID)p, blocks[i].change_len, PAGE_EXECUTE_READWRITE, &flOldProtect))
			{
				// we now write a short jump which skips over the
				// native call which is done for LayoutWindows(..)
				// so our version is used without conflict from it
				DWORD written = 0;
				WriteProcessMemory(curproc, (LPVOID)p, blocks[i].change, blocks[i].change_len, &written);
				VirtualProtect((LPVOID)p, blocks[i].change_len, flOldProtect, &flDontCare);
			}
		}
	}
}

void StartProcessingFile(const wchar_t * szFn, BOOL start_playing)
{
	HMODULE hDLL = NULL;
	pModule = NULL;

	// we use Winamp's own checking to more reliably ensure that we'll
	// get which plug-in is actually responsible for the file handling
	In_Module *in_mod = (In_Module*)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)szFn, IPC_CANPLAY);
	if (in_mod && (in_mod != (In_Module*)1))
	{
		// try to not do the duplicated plug-in method if
		// there is nothing playing at the current time
		// TODO need to be smarter to check if it's the
		//		same as the plug-in which has been found
		/*if (!start_playing && !SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING))
		{
			pModule = in_mod;
		}
		else*/
		{
			wchar_t szSource[MAX_PATH] = {0};
			GetModuleFileName(in_mod->hDllInstance, szSource, ARRAYSIZE(szSource));

			// we got a valid In_Module * so make a temp copy
			// which we'll then be calling for the processing
			wchar_t *filename = PathFindFileName(szSource);

			if (StrStrI(filename, L"in_midi.dll") ||
				StrStrI(filename, L"in_wm.dll"))
			{
				// these native nullsoft plug-in really
				// really really doesn't like to work as a
				// multi-instance plug-in so best to just
				// not attempt to use it for processing :(
				//
				// somehow leveraging the transcoding api
				// might be enough to re-enable in_wm.dll
				bUnsupported = 2;
				return;
			}

			PathCombine(szTempDLLDestination, szWaveCacheDir, L"waveseek_");
			PathAddExtension(szTempDLLDestination, filename);
			// if not there then copy it
			if (!PathFileExists(szTempDLLDestination))
			{
				CopyFile(szSource, szTempDLLDestination, FALSE);
			}
			// otherwise check there's not a difference in the
			// file times which means if the original plug-in
			// gets updated then our in-cache copy gets updated
			// and this way we're only copying the plug-in once
			else
			{
				HANDLE sourceFile = CreateFile(szSource, GENERIC_READ, FILE_SHARE_READ, NULL,
											   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL),
					   destFile = CreateFile(szTempDLLDestination, GENERIC_READ, FILE_SHARE_READ,
											 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

				FILETIME sourceTime = {0}, destTime = {0};
				GetFileTime(sourceFile, NULL, NULL, &sourceTime);
				GetFileTime(destFile, NULL, NULL, &destTime);

				CloseHandle(sourceFile);
				CloseHandle(destFile);

				// there's a difference so we will re-copy
				if (CompareFileTime(&sourceTime, &destTime))
				{
					CopyFile(szSource, szTempDLLDestination, FALSE);
				}
			}

			if (PathFileExists(szTempDLLDestination))
			{
				hDLL = GetModuleHandle(szTempDLLDestination);
				if (!hDLL)
				{
					hDLL = LoadLibrary(szTempDLLDestination);
				}
				if (!hDLL)
				{
					return;
				}

				if (StrStrI(filename, L"mpg123"))
				{
					// this allows us to patch the mpg123 based input plug-in
					// (different versions) so we can correct an issue it has
					// with the use of the fake output not being called once
					// the first file has been processed which was ok when we
					// unloaded the input plug-in after use (which isn't good
					// for most other plug-ins out there i.e. hang on close!)
					MPG123HotPatch(hDLL);
				}

				PluginGetter pluginGetter = (PluginGetter)GetProcAddress(hDLL, "winampGetInModule2");
				if (!pluginGetter)
				{
					return;
				}

				pModule = pluginGetter();
			}
		}
	}

	if (pModule)
	{
		pModule->hMainWindow = hWndWaveseek;
		pModule->hDllInstance = hDLL;

		pModule->dsp_isactive = DummyDSPIsActive;
		pModule->dsp_dosamples = DummyDSPDoSamples;
		pModule->SAVSAInit = DummySAVSAInit;
		pModule->SAVSADeInit = DummySAVSADeInit;
		pModule->SAAddPCMData = DummySAAddPCMData;
		pModule->SAGetMode = DummySAGetMode;
		pModule->SAAdd = DummySAAdd;
		pModule->VSAAddPCMData = DummyVSAAddPCMData;
		pModule->VSAGetMode = DummyVSAGetMode;
		pModule->VSAAdd = DummyVSAAdd;
		pModule->VSASetInfo = DummyVSASetInfo;
		pModule->SetInfo = DummySetInfo;

		// if a v5.66x+ input plug-in then fill the 'service' member
		if (((pModule->version & ~IN_UNICODE) & ~IN_INIT_RET) == 0x101)
		{
			pModule->service = WASABI_API_SVC;
		}

		const bool has_ret = !!(pModule->version & IN_INIT_RET);
		const int ret = pModule->Init();
		if ((has_ret && (ret == IN_INIT_SUCCESS)) || !has_ret)
		{
			if (pModule->UsesOutputPlug & IN_MODULE_FLAG_USES_OUTPUT_PLUGIN)
			{
				char szFile[MAX_PATH] = {0};
				const bool unicode = !!(pModule->version & IN_UNICODE);
				nLengthInMS = GetFileInfo(unicode, (char*)szFn, szFile);
				if (nLengthInMS <= 0)
				{
					return;
				}

				pModule->outMod = CreateOutput(hWndWaveseek, hDLL);
				if (pModule->Play((unicode ? (char*)szFn : szFile)) != 0)
				{
					return;
				}

				bIsProcessing = true;
			}
		}
	}
}

void ProcessStop()
{
	if (pModule)
	{
		if (bIsProcessing)
		{
			pModule->Stop();
		}
		pModule->Quit();
		//FreeLibrary(pModule->hDllInstance);
		pModule = NULL;
	}
	bIsProcessing = false;
}

void FinishProcessingFile()
{
	HANDLE h = CreateFile(szWaveCacheFile, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, NULL, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		DWORD dw = 0;
		WriteFile(h, pSampleBuffer, SAMPLE_BUFFER_SIZE * sizeof(unsigned short), &dw, NULL);
		CloseHandle(h);

		if (dw > 0)
		{
			bIsLoaded = true;
		}
	}

	ProcessStop();
}

typedef struct {
	wchar_t szPerformer[256];
	wchar_t szTitle[256];
	unsigned int nMillisec;
	bool bDrawn;
} CUETRACK;

int nCueTracks = 0;
CUETRACK pCueTracks[256] = {0};

void LoadCUE(wchar_t * szFn)
{
	FILE * f = _wfopen( szFn, L"rt" );
	if (!f)
	{
		return;
	}

	nCueTracks = 0;
	int nCurrentTrack = 0;
	wchar_t strs[256] = {0};
	while (fgetws(strs, 256, f))
	{
		wchar_t *str = strs;
		while (iswspace(*str))
		{
			++str;
		}

		if (wcsstr(str, L"TRACK") == str)
		{
			swscanf(str, L"TRACK %d AUDIO", &nCurrentTrack);
			nCurrentTrack = min(nCurrentTrack, 255);
			nCueTracks = max(nCueTracks, nCurrentTrack);
			pCueTracks[nCurrentTrack - 1].szPerformer[0] = 0;
			pCueTracks[nCurrentTrack - 1].szTitle[0] = 0;
		}

		if (nCurrentTrack > 0)
		{
			CUETRACK & track = pCueTracks[ nCurrentTrack - 1];
			if (wcsstr(str, L"PERFORMER") == str)
			{
				swscanf(str, L"PERFORMER \"%[^\"]\"", track.szPerformer);
			}

			if (wcsstr(str, L"TITLE") == str)
			{
				swscanf(str, L"TITLE \"%[^\"]\"", track.szTitle);
			}

			if (wcsstr(str, L"INDEX") == str)
			{
				int m = 0, s = 0, f = 0;
				swscanf(str, L"INDEX %*d %d:%d:%d", &m, &s, &f);
				track.nMillisec = m * 60 * 1000 + s * 1000 + (f * 1000) / 75;
			}
		}
	}
	fclose(f);
}

LPWSTR GetTooltipText(HWND hWnd, int pos, int lengthInMS)
{
	static wchar_t coords[256] = {0};
	RECT rc = {0};
	GetClientRect(hWnd, &rc);

	// adjust width down by 1px so that the tooltip should then
	// appear when the mouse is at the far right of the window.
	unsigned int cur_ms = MulDiv(pos, lengthInMS, (rc.right - rc.left) - 1),
				 sec = (cur_ms > 0 ? (cur_ms / 1000) : 0),
				 total_sec = (lengthInMS > 0 ? (lengthInMS / 1000) : 0);

	int nTrack = -1;
	for (int i = 0; i < nCueTracks; i++)
	{
		if (i < nCueTracks - 1)
		{
			if (pCueTracks[i].nMillisec <= cur_ms && cur_ms < pCueTracks[i + 1].nMillisec)
			{
				nTrack = i;
			}
		}
		else
		{
			if (pCueTracks[i].nMillisec <= cur_ms)
			{
				nTrack = i;
			}
		}
	}

	if (nTrack >= 0)
	{
		if (pCueTracks[nTrack].szPerformer[0])
		{
			StringCchPrintf(coords, ARRAYSIZE(coords), L"%s - %s [%d:%02d / %d:%02d]",
							pCueTracks[nTrack].szPerformer, pCueTracks[nTrack].szTitle,
							sec / 60, sec % 60, total_sec / 60, total_sec % 60);
		}
		else
		{
			StringCchPrintf(coords, ARRAYSIZE(coords), L"%s [%d:%02d / %d:%02d]",
							pCueTracks[nTrack].szTitle,
							sec / 60, sec % 60, total_sec / 60, total_sec % 60);
		}
	}
	else
	{
		StringCchPrintf(coords, ARRAYSIZE(coords), L"%d:%02d / %d:%02d",
						sec / 60, sec % 60, total_sec / 60, total_sec % 60);
	}

	return coords;
}

int GetFileLength()
{
	basicFileInfoStructW bfiW = {0};
	bfiW.filename = szFilename;
	if (!SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&bfiW, IPC_GET_BASIC_FILE_INFOW))
	{
		if (bfiW.length != -1)
		{
			return (bfiW.length * 1000);
		}
	}
	return -1000;
}

void ProcessFilePlayback(const wchar_t * szFn, BOOL start_playing)
{
	if (StrStrI(szFilename, szFn) && bIsProcessing)
	{
		// if we're already processing and we're asked
		// to re-process (e.g. multiple clicks in the
		// main playlist editor) then we try to filter
		// out and keep going if it's the same file.
		return;
	}

	ProcessStop();

	bIsProcessing = bIsLoaded = bUnsupported = false;
	bIsCurrent = !lstrcmpi(szFn, (wchar_t*)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_PLAYING_FILENAME));
	lstrcpyn(szFilename, szFn, MAX_PATH);

	nCueTracks = 0;

	// make sure that it's valid and something we can process
	if (szFilename[0] && !PathIsURL(szFn))
	{
		wchar_t szCue[MAX_PATH] = {0};
		lstrcpyn(szCue, szFn, MAX_PATH);
		PathRenameExtension(szCue, L".cue");

		if (PathFileExists(szCue))
		{
			LoadCUE(szCue);
		}

		PathCombine(szWaveCacheFile, szWaveCacheDir, PathFindFileName(szFn));
		StringCchCat(szWaveCacheFile, MAX_PATH, L".cache");

		if (!PathFileExists(szWaveCacheFile))
		{
			StartProcessingFile(szFn, start_playing);
		}
		else
		{
			HANDLE h = CreateFile(szWaveCacheFile, GENERIC_READ, NULL, NULL,
								  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (h != INVALID_HANDLE_VALUE)
			{
				DWORD dw = 0;
				ReadFile(h, pSampleBuffer, SAMPLE_BUFFER_SIZE * sizeof(unsigned short), &dw, NULL);
				CloseHandle(h);
				if (dw > 0)
				{
					bIsLoaded = true;
				}
			}
		}
	}

	// if the tooltip is being shown then we need to try
	// & update it so that it reflects the current view.
	if (!hideTooltip && IsWindowVisible(hWndToolTip))
	{
		const int lengthInMS = (bIsCurrent ? SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME) : GetFileLength());
		POINT pt = {0};
		GetCursorPos(&pt);
		ScreenToClient(hWndInner, &pt);
		ti.lpszText = GetTooltipText(hWndInner, pt.x, lengthInMS);
		SendMessage(hWndToolTip, TTM_SETTOOLINFO, 0, (LPARAM)&ti);
	}
}

HFONT GetWaveformFont()
{
	if (!hFont)
	{
		INT height = (INT)SendMessageW(plugin.hwndParent, WM_WA_IPC, 3, IPC_GET_GENSKINBITMAP);
		DWORD charset = (DWORD)SendMessageW(plugin.hwndParent, WM_WA_IPC, 2, IPC_GET_GENSKINBITMAP);
		char *fontname = (char*)SendMessageW(plugin.hwndParent, WM_WA_IPC, 1, IPC_GET_GENSKINBITMAP);
		hFont = CreateFontA(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, charset,
							OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DRAFT_QUALITY,
							DEFAULT_PITCH | FF_DONTCARE, fontname);
	}
	return hFont;
}

void ProcessSkinChange()
{
	WADlg_init(plugin.hwndParent);

	clrBackground = WADlg_getColor(WADLG_ITEMBG);
	clrCuePoint = WADlg_getColor(WADLG_HILITE);
	clrGeneratingText = (COLORREF)SendMessage(plugin.hwndParent, WM_WA_IPC, 4, IPC_GET_GENSKINBITMAP);

	// get the current skin and use that as a
	// means to control the colouring used
	wchar_t szBuffer[MAX_PATH] = {0};
	SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)szBuffer, IPC_GETSKINW);

	if (szBuffer[0])
	{
		// use the skin provided value
		clrWaveformPlayed = clrGeneratingText;
	}
	else
	{
		// use a default value as the default
		// colour from the classic base skin
		// is almost white and looks wrong.
		clrWaveformPlayed = RGB(0, 178, 0);
	}

	clrWaveform = WADlg_getColor(WADLG_ITEMFG);
	// try to ensure that things can be seen in-case of
	// close colouring by the skin (matches what the ML
	// tries to do to ensure some form of visibility).
	if (abs(GetColorDistance(clrWaveformPlayed, clrWaveform)) < 70)
	{ 
		clrWaveformPlayed = BlendColors(clrBackground, clrWaveform, (COLORREF)77);
	}

	if (hbrBackground)
	{
		DeleteBrush(hbrBackground);
	}
	hbrBackground = CreateSolidBrush(clrBackground);

	if (hFont)
	{
		DeleteFont(hFont);
		hFont = NULL;
	}
	GetWaveformFont();
}

void PaintWaveform(HDC hdc, RECT rc)
{
	int nSongPos = (bIsCurrent ? SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME) : 0),
		nSongLen = (bIsCurrent ? SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME) : GetFileLength()),
		nBufPos = (nSongLen != -1 ? MulDiv(nSongPos, SAMPLE_BUFFER_SIZE, nSongLen) : 0),
		w = (rc.right - rc.left), h = (rc.bottom - rc.top);

	const HDC hdcMem = CreateCompatibleDC(hdc);
	const HBITMAP hbmMem = CreateCompatibleBitmap(hdc, w, h),
				  hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

	SelectObject(hdcMem, GetWaveformFont());

	FillRect(hdcMem, &rc, hbrBackground);

	if ((bIsLoaded || bIsProcessing) && !bUnsupported)
	{
		// make the width a bit less so the right-edge
		// can allow for the end of the track to be
		// correctly selected or the tooltip show up
		--w;

		SelectObject(hdcMem, GetStockObject(DC_PEN));

		for (int i = 0; i < w; i++)
		{
			const int nBufLoc0 = ((i * SAMPLE_BUFFER_SIZE) / w),
					  nBufLoc1 = min((((i + 1) * SAMPLE_BUFFER_SIZE) / w), SAMPLE_BUFFER_SIZE);

			SetDCPenColor(hdcMem, ((nBufLoc0 < nBufPos) ? clrWaveformPlayed : clrWaveform));

			unsigned short nSample = 0;
			for (int j = nBufLoc0; j < nBufLoc1; j++)
			{
				nSample = max(pSampleBuffer[j], nSample);
			}

			const unsigned short sh = ((nSample * h) / 32767);
			const int y = (h - sh) / 2;
			MoveToEx(hdcMem, i, y, NULL);
			LineTo(hdcMem, i, y + sh);

			if (showCuePoints && nCueTracks > 0)
			{
				unsigned int ms = MulDiv(i, nSongLen, w);
				if (ms > 0)
				{
					for (int k = 0; k <= nCueTracks; k++)
					{
						if (!pCueTracks[k].bDrawn && (pCueTracks[k].nMillisec > 0) && (pCueTracks[k].nMillisec < ms))
						{
							pCueTracks[k].bDrawn = true;
							SetDCPenColor(hdcMem, clrCuePoint);
							MoveToEx(hdcMem, i, y, NULL);
							LineTo(hdcMem, i, h);
							MoveToEx(hdcMem, i, y + sh, NULL);
							break;
						}
					}
				}
			}
		}

		for (int k = 0; k < nCueTracks; k++)
		{
			pCueTracks[k].bDrawn = false;
		}

		// and now restore so that we get drawn correctly
		++w;
	}
	else
	{
		SetBkColor(hdcMem, clrBackground);
		SetTextColor(hdcMem, clrGeneratingText);

		DrawText(hdcMem, (!PathIsURL(szFilename) ? (bUnsupported == 2 ? szBadPlugin :
													szUnavailable) : szStreamsNotSupported),
				 -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

	SelectObject(hdcMem, hbmOld);
	DeleteObject(hbmMem);
	DeleteDC(hdcMem);
}

LRESULT sendMlIpc(int msg, WPARAM param)
{
	static LRESULT IPC_GETMLWINDOW;
	if (!IPC_GETMLWINDOW)
	{
		IPC_GETMLWINDOW = SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&"LibraryGetWnd", IPC_REGISTER_WINAMP_IPCMESSAGE);
	}
	HWND mlwnd = (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETMLWINDOW);

	if ((param == 0) && (msg == 0))
	{
		return (LRESULT)mlwnd;
	}

	if (IsWindow(mlwnd))
	{
		return SendMessage(mlwnd, WM_ML_IPC, param, msg);
	}

	return 0;
}

BOOL Menu_TrackPopup(HMENU hMenu, UINT fuFlags, int x, int y, HWND hwnd)
{
	if (hMenu == NULL)
	{
		return NULL;
	}

	if (IsWindow((HWND)sendMlIpc(0, 0)))
	{
		MLSKINNEDPOPUP popup = {sizeof(MLSKINNEDPOPUP)};
		popup.hmenu = hMenu;
		popup.fuFlags = fuFlags;
		popup.x = x;
		popup.y = y;
		popup.hwnd = hwnd;
		popup.skinStyle = SMS_USESKINFONT;
		return (INT)sendMlIpc(ML_IPC_TRACKSKINNEDPOPUPEX, (WPARAM)&popup);
	}
	return TrackPopupMenu(hMenu, fuFlags, x, y, 0, hwnd, NULL);
}

bool ProcessMenuResult(UINT command, HWND parent)
{
	switch (LOWORD(command))
	{
		case ID_SUBMENU_VIEWFILEINFO:
		{
			infoBoxParamW infoBoxW = {hWndWaveseek, szFilename};
			SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&infoBoxW, IPC_INFOBOXW);
			break;
		}
		case ID_SUBMENU_CLEARWAVCACHE:
		{
			if (MessageBox(parent, WASABI_API_LNGSTRINGW(IDS_CLEAR_CACHE),
						   (LPWSTR)plugin.description, MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				break;
			}

			// remove all *.cache files in the users current cache folder.
			//
			// removing the waveseek_in_*.dll isn't covered as they are
			// still loaded and the unload / force delete will cause a
			// crash and so is not worth the hassle to do it. plus it
			// updates automatically as needed with the copy file checks :)
			WIN32_FIND_DATAW wfd = {0};
			wchar_t szFnFind[MAX_PATH] = {0};
			PathCombine(szFnFind, szWaveCacheDir, L"*.cache");
			HANDLE hFind = FindFirstFile(szFnFind, &wfd);

			if (hFind != INVALID_HANDLE_VALUE)
			{
				do
				{
					PathCombine(szFnFind, szWaveCacheDir, wfd.cFileName);
					DeleteFile(szFnFind);
				}
				while (FindNextFile(hFind, &wfd));
				FindClose(hFind);
			}

			// we will want to fall through so we
			// can then re-render the current file
		}
		case ID_SUBMENU_RERENDER:
		{
			if (!PathIsURL(szFilename))
			{
				wchar_t cacheFile[MAX_PATH] = {0};
				PathCombine(cacheFile, szWaveCacheDir, PathFindFileName(szFilename));
				StringCchCat(cacheFile, MAX_PATH, L".cache");
				if (PathFileExists(cacheFile))
				{
					DeleteFile(cacheFile);
				}

				ProcessFilePlayback(szFilename, TRUE);
			}
			break;
		}
		case ID_CONTEXTMENU_CLICKTRACK:
		{
			clickTrack = (!clickTrack);
			WritePrivateProfileInt(L"Waveseek", L"clickTrack", clickTrack, ini_file);

			// update as needed to match the new setting
			// with fallback to the current playing if
			// there's no selection or it's been disabled
			int index = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);
			if (clickTrack)
			{
				if (SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_PLAYLIST_GET_SELECTED_COUNT))
				{
					const int sel = (int)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_PLAYLIST_GET_NEXT_SELECTED);
					if (sel != -1)
					{
						index = sel;
					}
				}
			}
			ProcessFilePlayback((const wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, index, IPC_GETPLAYLISTFILEW), FALSE);
			break;
		}
		case ID_SUBMENU_SHOWCUEPOINTS:
		{
			showCuePoints = (!showCuePoints);
			WritePrivateProfileInt(L"Waveseek", L"showCuePoints", showCuePoints, ini_file);
			break;
		}
		case ID_SUBMENU_HIDEWAVEFORMTOOLTIP:
		{
			hideTooltip = (!hideTooltip);
			WritePrivateProfileInt(L"Waveseek", L"hideTooltip", hideTooltip, ini_file);
			break;
		}
		case ID_SUBMENU_ABOUT:
		{
			wchar_t message[512] = {0};
			StringCchPrintf(message, ARRAYSIZE(message), WASABI_API_LNGSTRINGW(IDS_ABOUT_STRING), TEXT(__DATE__));
			MessageBox(plugin.hwndParent, message, pluginTitleW, 0);
			break;
		}
		default:
		{
			return false;
		}
	}
	return true;
}

INT_PTR CALLBACK EmdedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_WA_MPEG_EOF:
		{
			FinishProcessingFile();
			break;
		}
		case WM_WA_IPC:
		{
			// this is used to pass on some of the messages but not all
			// since we use this window as a fake 'main' window so some
			// of the messages like WM_WA_MPEG_EOF are blocked from the
			// real main window when using the dll re-use hack in place
			return SendMessage(plugin.hwndParent, uMsg, wParam, lParam);
		}
		case WM_COMMAND:	// for what's handled from the accel table
		{
			if (ProcessMenuResult(wParam, hWnd))
			{
				break;
			}
		}
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_CHAR:
		case WM_MOUSEWHEEL:
		{
			PostMessage(plugin.hwndParent, uMsg, wParam, lParam);
			break;
		}
		case WM_CONTEXTMENU:
		{
			short xPos = GET_X_LPARAM(lParam);
			short yPos = GET_Y_LPARAM(lParam);

			HMENU hMenu = WASABI_API_LOADMENUW(IDR_CONTEXTMENU);
			HMENU popup = GetSubMenu(hMenu, 0);
			CheckMenuItem(popup, ID_CONTEXTMENU_CLICKTRACK, MF_BYCOMMAND | (clickTrack ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem(popup, ID_SUBMENU_SHOWCUEPOINTS, MF_BYCOMMAND | (showCuePoints ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem(popup, ID_SUBMENU_HIDEWAVEFORMTOOLTIP, MF_BYCOMMAND | (hideTooltip ? MF_CHECKED : MF_UNCHECKED));

			// this will handle the menu being shown not via the mouse actions
			// so is positioned just below the header if no selection but there's a queue
			// or below the item selected (or the no files in queue entry)
			if (xPos == -1 || yPos == -1)
			{
				RECT rc = {0};
				GetWindowRect(GetWindow(hWnd, GW_CHILD), &rc);
				xPos = (short)rc.left;
				yPos = (short)rc.top;
			}

			ProcessMenuResult(Menu_TrackPopup(popup, TPM_LEFTALIGN | TPM_LEFTBUTTON |
							  TPM_RIGHTBUTTON | TPM_RETURNCMD, xPos, yPos, hWnd), hWnd);

			DestroyMenu(hMenu);
			break;
		}
	}

	return CallWindowProc(lpWndProc, hWnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK InnerWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// if you need to do other message handling then you can just place this first and
	// process the messages you need to afterwards. note this is passing the frame and
	// its id so if you have a few embedded windows you need to do this with each child
	if (HandleEmbeddedWindowChildMessages(hWndWaveseek, WINAMP_WAVEFORM_SEEK_MENUID,
										  hWnd, uMsg, wParam, lParam))
	{
		return 0;
	}

	bool bForceJump = false;
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			hWndToolTip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
										 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
										 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
										 CW_USEDEFAULT, (HWND)lParam, NULL,
										 plugin.hDllInstance, NULL);

			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
			ti.hwnd = (HWND)lParam;
			ti.hinst = plugin.hDllInstance;
			ti.uId = (UINT_PTR)lParam;
			PostMessage(hWndToolTip, TTM_ADDTOOL, NULL, (LPARAM)&ti);

			SetTimer(hWnd, TIMER_ID, TIMER_FREQ, NULL);
			SetWindowLongPtr(hWnd, GWLP_ID, 101);
			break;
		}
		case WM_TIMER:
		{
			if (wParam == TIMER_ID)
			{
				InvalidateRect(hWnd, NULL, TRUE);
			}
			break;
		}
		case WM_ERASEBKGND:
		{
			RECT rc = {0};
			GetClientRect(hWnd, &rc);
			PaintWaveform((HDC)wParam, rc);
			return 1;
		}
		case WM_LBUTTONDBLCLK:
		{
			// start playing current or selection as needed
			// this also jumps to the time point where the click
			// happened as a coincidence of the mesages received
			if (bIsCurrent)
			{
				if (!SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING))
				{
					SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(WINAMP_BUTTON2, 0), 0);
					bForceJump = true;
				}
				// we'll fall through to WM_LBUTTONUP the handling
				// so that we also do jump to the desired point of
				// the double-click
			}
			else
			{
				if (SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_PLAYLIST_GET_SELECTED_COUNT))
				{
					const int sel = (int)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_PLAYLIST_GET_NEXT_SELECTED);
					if (sel != -1)
					{
						// update the position and then fake hitting play
						SendMessage(plugin.hwndParent, WM_WA_IPC, sel, IPC_SETPLAYLISTPOS);
						PostMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(WINAMP_BUTTON2, 0), 0);
					}
				}
				break;
			}
		}
		case WM_LBUTTONUP:
		{
			if (bIsCurrent)
			{
				const int nSongLen = (int)SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME);
				if (nSongLen != -1 || bForceJump)
				{
					RECT rc = {0};
					GetClientRect(hWnd, &rc);
					const unsigned int ms = MulDiv(GET_X_LPARAM(lParam), nSongLen, (rc.right - rc.left));
					// if not forcing a jump then just send as-is
					if (!bForceJump)
					{
						SendMessage(plugin.hwndParent, WM_WA_IPC, ms, IPC_JUMPTOTIME);
					}
					else
					{
						// but if we need to force due to a double-click start
						// then we need to wait a little bit before we can try
						// to seek else it'll fail as the file won't be in the
						// playing state so soon after sending start playback.
						Sleep(100);
						PostMessage(plugin.hwndParent, WM_WA_IPC, ms, IPC_JUMPTOTIME);
					}
				}
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (!hideTooltip)
			{
				static short xOldPos = 0; 
				static short yOldPos = 0; 
				short xPos = GET_X_LPARAM(lParam); 
				short yPos = GET_Y_LPARAM(lParam); 

				if (xPos == xOldPos && yPos == yOldPos)
				{
					break;
				}

				xOldPos = xPos;
				yOldPos = yPos;

				const int lengthInMS = (bIsCurrent ? SendMessage(plugin.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME) : GetFileLength());
				if (lengthInMS > 0)
				{
					// ensures we'll get a WM_MOUSELEAVE 
					TRACKMOUSEEVENT trackMouse = {0};
					trackMouse.cbSize = sizeof(trackMouse);
					trackMouse.dwFlags = TME_LEAVE;
					trackMouse.hwndTrack = hWnd;
					TrackMouseEvent(&trackMouse);

					POINT pt = {xPos, yPos};
					ClientToScreen(hWndWaveseek, &pt);
					ti.lpszText = GetTooltipText(hWnd, xPos, lengthInMS);

					SendMessage(hWndToolTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
					SendMessage(hWndToolTip, TTM_SETTOOLINFO, 0, (LPARAM)&ti);
					SendMessage(hWndToolTip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x + 11, pt.y - 2));
				}
			}
			break;
		}
		case WM_MOUSELEAVE:
		{
			SendMessage(hWndToolTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
			break;
		}
	}
	return 0;
}


int IsInPlaylistArea(HWND playlist_wnd, int mode)
{
	POINT pt = {0};
	RECT rc = {0};

	GetCursorPos(&pt);
	GetClientRect(playlist_wnd, &rc);

	ScreenToClient(playlist_wnd, &pt);
	// this corrects so the selection works correctly on the selection boundary
	pt.y -= 2;

	if (!mode)
	{
		// corrects for the window area so it only happens if a selection happens
		rc.top += 18;
		rc.left += 12;
		rc.right -= 19;
		rc.bottom -= 40;
		return PtInRect(&rc, pt);
	}
	else
	{
		rc.bottom -= 13;
		rc.top = rc.bottom - 19;
		rc.left += 14;

		for (int i = 0; i < 4; i++)
		{
			rc.right = rc.left + 22;
			if (PtInRect(&rc, pt))
			{
				return 1;	
			}
			else
			{
				rc.left = rc.right + 7;
			}
		}
		return 0;
	}
}

static LRESULT WINAPI SubclassPlaylistProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// this will detect false clicks such as when the menus are shown in the classic playlist editor
	if (msg == WM_LBUTTONDOWN)
	{
		if (IsInPlaylistArea(hwnd, 1))
		{
			on_click = 1;
		}
	}

	LRESULT ret = CallWindowProc(oldPlaylistWndProc, hwnd, msg, wParam, lParam);

	// this will then handle the detection of a proper click in the playlist editor so
	// if enabled then we can get and show the album art for the selected playlist item
	if (msg == WM_LBUTTONDOWN && !(GetKeyState(VK_MENU) & 0x1000) &&
								 !(GetKeyState(VK_SHIFT) & 0x1000) &&
								 !(GetKeyState(VK_CONTROL) & 0x1000))
	{
		if (!on_click && clickTrack)
		{
			POINT pt = {0};
			INT index = 0;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			// this corrects so the selection works correctly on the selection boundary
			pt.y -= 2;
			index = SendMessage(hwnd, WM_WA_IPC, IPC_PE_GETIDXFROMPOINT, (LPARAM)&pt);
			if (IsInPlaylistArea(hwnd, 0))
			{
				// bounds check things
				if (index < SendMessage(hwnd, WM_WA_IPC, IPC_PE_GETINDEXTOTAL, 0))
				{
					// art change to show the selected item in the playlist editor
					ProcessFilePlayback((const wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, index, IPC_GETPLAYLISTFILEW), FALSE);
				}
			}
		}
		else
		{
			// needs to do an increment for the next click will be a false
			on_click = ((on_click == 1) ? 2 : 0);
		}
	}
	else if (msg == WM_COMMAND)
	{
		// this is used for tracking the selection of items in the playlist editor
		// so we can update the album art when doing a single up/down selection
		if (((LOWORD(wParam) == ID_PE_SCUP) || (LOWORD(wParam) == ID_PE_SCDOWN)) && clickTrack)
		{
			// only do on up/down without any other keyboard accelerators pressed
			if (!(GetKeyState(VK_MENU) & 0x1000) &&
			    !(GetKeyState(VK_SHIFT) & 0x1000) &&
			    !(GetKeyState(VK_CONTROL) & 0x1000))
			{
				if (SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_PLAYLIST_GET_SELECTED_COUNT))
				{
					const int sel = (int)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)-1, IPC_PLAYLIST_GET_NEXT_SELECTED);
					if (sel != -1)
					{
						// art change to show the selected item in the playlist editor
						ProcessFilePlayback((const wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, sel, IPC_GETPLAYLISTFILEW), FALSE);
					}
				}
			}
		}
	}

	return ret;
}

LRESULT CALLBACK WinampHookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DISPLAYCHANGE)
	{
		ProcessSkinChange();
	}
	else if (uMsg == WM_WA_IPC)
	{
		// make sure we catch all appropriate skin changes
		if (lParam == IPC_SKIN_CHANGED ||
			lParam == IPC_CB_RESETFONT ||
			lParam == IPC_FF_ONCOLORTHEMECHANGED)
		{
			ProcessSkinChange();
		}
		else if (lParam == IPC_PLAYING_FILEW)
		{
			ProcessFilePlayback((const wchar_t*)wParam, TRUE);
		}
		else if (lParam == delay_load)
		{
			GetFilePaths();

			// just incase we need to handle a migration update, we check
			// for a Winamp\Plugins\wavecache folder and if found then we
			// move it into the correct settings folder (due to UAC, etc)
			if (!GetPrivateProfileInt(L"Migrate", 0))
			{
				wchar_t szOldWaveCacheDir[MAX_PATH] = {0};
				PathCombine(szOldWaveCacheDir, szDLLPath, L"wavecache");
				if (PathFileExists(szOldWaveCacheDir))
				{
					wchar_t szFnFind[MAX_PATH] = {0};
					PathCombine(szFnFind, szOldWaveCacheDir, L"*.cache");

					WIN32_FIND_DATA wfd = {0};
					HANDLE hFind = FindFirstFile(szFnFind, &wfd);
					if (hFind != INVALID_HANDLE_VALUE)
					{
						do
						{
							// if we found a *.cache file then move it over
							// as long as there's permission and the OS can
							wchar_t szFnMove[MAX_PATH] = {0};
							PathCombine(szFnFind, szOldWaveCacheDir, wfd.cFileName);
							PathCombine(szFnMove, szWaveCacheDir, wfd.cFileName);
							MoveFileEx(szFnFind, szFnMove, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
						}
						while (FindNextFile(hFind,&wfd));
						CloseHandle(hFind);
					}
				}
				if (PathIsDirectoryEmpty(szOldWaveCacheDir))
				{
					RemoveDirectory(szOldWaveCacheDir);
				}
				WritePrivateProfileString(L"Waveseek", L"Migrate", L"1", ini_file);
			}

			clickTrack = GetPrivateProfileInt(L"clickTrack", 1);
			showCuePoints = GetPrivateProfileInt(L"showCuePoints", 0);
			hideTooltip = GetPrivateProfileInt(L"hideTooltip", 0);

			ProcessSkinChange();

			// for the purposes of this example we will manually create an accelerator table so
			// we can use IPC_REGISTER_LOWORD_COMMAND to get a unique id for the menu items we
			// will be adding into Winamp's menus. using this api will allocate an id which can
			// vary between Winamp revisions as it moves depending on the resources in Winamp.
			WINAMP_WAVEFORM_SEEK_MENUID = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_REGISTER_LOWORD_COMMAND);

			// then we show the embedded window which will cause the child window to be
			// sized into the frame without having to do any thing ourselves. also this will
			// only show the window if Winamp was not minimised on close and the window was
			// open at the time otherwise it will remain hidden
			old_visible = visible = GetPrivateProfileInt(L"wnd_open", TRUE);

			// finally we add menu items to the main right-click menu and the views menu
			// with Modern skins which support showing the views menu for accessing windows
			AddEmbeddedWindowToMenus(TRUE, WINAMP_WAVEFORM_SEEK_MENUID, WASABI_API_LNGSTRINGW(IDS_WAVEFORM_SEEKER), -1);

			// now we will attempt to create an embedded window which adds its own main menu entry
			// and related keyboard accelerator (like how the media library window is integrated)
			hWndWaveseek = CreateEmbeddedWindow(&embed, embed_guid);

			// once the window is created we can then specify the window title and menu integration
			SetWindowText(hWndWaveseek, WASABI_API_LNGSTRINGW(IDS_WAVEFORM_SEEKER));

			lpWndProc = (WNDPROC)(LONG_PTR)SetWindowLongPtr(hWndWaveseek, GWLP_WNDPROC, (LONG_PTR)EmdedWndProc);

			HACCEL accel = WASABI_API_LOADACCELERATORSW(IDR_ACCELERATOR_WND);
			if (accel)
			{
				WASABI_API_APP->app_addAccelerators(hWndWaveseek, &accel, 1, TRANSLATE_MODE_NORMAL);
			}

			hWndInner = CreateDialogParam(plugin.hDllInstance, MAKEINTRESOURCE(IDD_VIEW),
										  hWndWaveseek, InnerWndProc, (LPARAM)hWndWaveseek);

			ProcessFilePlayback((const wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC,
								SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS),
								IPC_GETPLAYLISTFILEW), FALSE);

			HWND pe_wnd = (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND);
			oldPlaylistWndProc = (WNDPROC)SetWindowLongPtr(pe_wnd, GWLP_WNDPROC, (LONG_PTR)SubclassPlaylistProc);

			WASABI_API_LNGSTRINGW_BUF(IDS_WAVEFORM_UNAVAILABLE, szUnavailable, ARRAYSIZE(szUnavailable));
			WASABI_API_LNGSTRINGW_BUF(IDS_WAVEFORM_UNAVAILABLE_BAD_PLUGIN, szBadPlugin, ARRAYSIZE(szBadPlugin));
			WASABI_API_LNGSTRINGW_BUF(IDS_STREAMS_NOT_SUPPORTED, szStreamsNotSupported, ARRAYSIZE(szStreamsNotSupported));


			// Winamp can report if it was started minimised which allows us to control our window
			// to not properly show on startup otherwise the window will appear incorrectly when it
			// is meant to remain hidden until Winamp is restored back into view correctly
			if ((SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_INITIAL_SHOW_STATE) == SW_SHOWMINIMIZED))
			{
				SetEmbeddedWindowMinimizedMode(hWndWaveseek, TRUE);
			}
			else
			{
				// only show on startup if under a classic skin and was set
				if (visible)
				{
					ShowWindow(hWndWaveseek, SW_SHOW);
				}
			}
		}
	}

	// this will handle the message needed to be caught before the original window
	// proceedure of the subclass can process it. with multiple windows then this
	// would need to be duplicated for the number of embedded windows your handling
	HandleEmbeddedWindowWinampWindowMessages(hWndWaveseek, WINAMP_WAVEFORM_SEEK_MENUID,
												 &embed, TRUE, hWnd, uMsg, wParam, lParam);

	LRESULT ret = CallWindowProc(lpWndProcOld, hWnd, uMsg, wParam, lParam);

	// this will handle the message needed to be caught after the original window
	// proceedure of the subclass can process it. with multiple windows then this
	// would need to be duplicated for the number of embedded windows your handling
	HandleEmbeddedWindowWinampWindowMessages(hWndWaveseek, WINAMP_WAVEFORM_SEEK_MENUID,
											 &embed, FALSE, hWnd, uMsg, wParam, lParam);

	return ret;
}

int PluginInit() 
{
	/*WASABI_API_SVC = GetServiceAPIPtr();/*/
	// load all of the required wasabi services from the winamp client
	WASABI_API_SVC = reinterpret_cast<api_service*>(SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE));
	if (WASABI_API_SVC == reinterpret_cast<api_service*>(1)) WASABI_API_SVC = NULL;/**/
	if (WASABI_API_SVC != NULL)
	{
		ServiceBuild(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);
		// TODO add to lang.h
		WASABI_API_START_LANG(plugin.hDllInstance, embed_guid);

		StringCchPrintf(pluginTitleW, ARRAYSIZE(pluginTitleW), WASABI_API_LNGSTRINGW(IDS_PLUGIN_NAME), TEXT(PLUGIN_VERSION));
		plugin.description = (char*)pluginTitleW;

		ServiceBuild(WASABI_API_SVC, WASABI_API_APP, applicationApiServiceGuid);

		lpWndProcOld = (WNDPROC)SetWindowLongPtr(plugin.hwndParent, GWLP_WNDPROC, (LONG_PTR)WinampHookWndProc);

		// restore / process the current file so we're showing something on load
		// but we delay it a bit until Winamp is in a better state especially if
		// we then fire offa file processing action otherwise we slow down startup
		delay_load = (int)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&"wave_seeker", IPC_REGISTER_WINAMP_IPCMESSAGE);
		PostMessage(plugin.hwndParent, WM_WA_IPC, 0, delay_load);

		return GEN_INIT_SUCCESS;
	}
	return GEN_INIT_FAILURE;
}

void PluginConfig()
{
	HMENU hMenu = WASABI_API_LOADMENUW(IDR_CONTEXTMENU);
	HMENU popup = GetSubMenu(hMenu, 0);
	RECT r = {0};

	MENUITEMINFO i = {sizeof(i), MIIM_ID | MIIM_STATE | MIIM_TYPE, MFT_STRING, MFS_UNCHECKED | MFS_DISABLED, 1};
	i.dwTypeData = pluginTitleW;
	InsertMenuItem(popup, 0, TRUE, &i);

	// as we are re-using the same menu resource, we
	// need to remove the options that are not global
	DeleteMenu(popup, ID_SUBMENU_RERENDER, MF_BYCOMMAND);
	DeleteMenu(popup, ID_SUBMENU_VIEWFILEINFO, MF_BYCOMMAND);

	HWND list =	FindWindowEx(GetParent(GetFocus()), 0, L"SysListView32", 0);
	ListView_GetItemRect(list, ListView_GetSelectionMark(list), &r, LVIR_BOUNDS);
	ClientToScreen(list, (LPPOINT)&r);

	CheckMenuItem(popup, ID_CONTEXTMENU_CLICKTRACK, MF_BYCOMMAND | (clickTrack ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(popup, ID_SUBMENU_SHOWCUEPOINTS, MF_BYCOMMAND | (showCuePoints ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(popup, ID_SUBMENU_HIDEWAVEFORMTOOLTIP, MF_BYCOMMAND | (hideTooltip ? MF_CHECKED : MF_UNCHECKED));
	ProcessMenuResult(TrackPopupMenu(popup, TPM_RETURNCMD | TPM_LEFTBUTTON, r.left, r.top, 0, list, NULL), list);

	DestroyMenu(hMenu);
}

void PluginQuit()
{
	ProcessStop();

	if (no_uninstall)
	{
		/*WritePrivateProfileInt(L"Waveseek", L"PosX", embed.r.left, ini_file);
		WritePrivateProfileInt(L"Waveseek", L"PosY", embed.r.top, ini_file);
		WritePrivateProfileInt(L"Waveseek", L"SizeX", (embed.r.right - embed.r.left), ini_file);
		WritePrivateProfileInt(L"Waveseek", L"SizeY", (embed.r.bottom - embed.r.top), ini_file);*/
		DestroyEmbeddedWindow(&embed);
	}

	if (IsWindow(hWndWaveseek))
	{
		KillTimer(hWndWaveseek, TIMER_ID);
		DestroyWindow(hWndToolTip);
		DestroyWindow(hWndWaveseek);
	}

	if (hbrBackground)
	{
		DeleteObject(hbrBackground);
		hbrBackground = NULL;
	}

	if (hFont)
	{
		DeleteFont(hFont);
		hFont = NULL;
	}

	if (PathFileExists(szTempDLLDestination))
	{
		DeleteFile(szTempDLLDestination);
	}

	ServiceRelease(WASABI_API_SVC, WASABI_API_APP, applicationApiServiceGuid);
}

winampGeneralPurposePlugin plugin =
{
	GPPHDR_VER_U,
	(char *)L"Waveform Seeker v2.0",
	PluginInit,
	PluginConfig,
	PluginQuit,
	NULL,
	NULL,
};

extern "C" __declspec( dllexport ) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin()
{
	return &plugin;
}

extern "C"__declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	// prompt to remove our settings with default as no (just incase)
	if (MessageBox(hwndDlg, WASABI_API_LNGSTRINGW(IDS_DO_YOU_ALSO_WANT_TO_REMOVE_SETTINGS),
				   pluginTitleW, MB_YESNO | MB_DEFBUTTON2) == IDYES)
	{
		WritePrivateProfileString(L"Waveseek", 0, 0, ini_file);
		no_uninstall = 0;
	}

	// as we're doing too much in subclasses, etc we cannot allow for on-the-fly removal so need to do a normal reboot
	return GEN_PLUGIN_UNINSTALL_REBOOT;
}

#ifndef _DEBUG
BOOL WINAPI _DllMainCRTStartup(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
	}
	return TRUE;
}
#endif