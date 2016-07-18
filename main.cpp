#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <Shlwapi.h>
#include <winamp/IN2.H>
#include <winamp/gen.h>
#include <winamp/wa_ipc.h>
#include <gen_ml/ml.h>
#include <gen_ml/ml_ipc_0313.h>
#include <nu/AutoWide.h>
#include <nu/AutoChar.h>
#include "resource.h"
#include "gen_waveseek.h"

#define WA_DLG_IMPLEMENT
#include <winamp/wa_dlg.h>
#include <Strsafe.h>
#include <commctrl.h>

#ifndef LONGX86
#ifdef _WIN64
#define LONGX86 LONG_PTR
#else /*_WIN64*/
#define LONGX86  LONG 
#endif  /*_WIN64*/
#endif // LONGX86

HWND hWndWaveseek = NULL;
HWND hWndToolTip = NULL;
embedWindowState embed = {0};

// {1C2F2C09-4F43-4CFF-9DE7-32E014638DFC}
static const GUID embed_guid = 
{ 0x1c2f2c09, 0x4f43, 0x4cff, { 0x9d, 0xe7, 0x32, 0xe0, 0x14, 0x63, 0x8d, 0xfc } };

void DummySAVSAInit(int maxlatency_in_ms, int srate){}
void DummySAVSADeInit(){}
void DummySAAddPCMData(void *PCMData, int nch, int bps, int timestamp){} 
int DummySAGetMode(){ return 0; }
int DummySAAdd(void *data, int timestamp, int csa){ return 0; }
void DummyVSAAddPCMData(void *PCMData, int nch, int bps, int timestamp){}
int DummyVSAGetMode(int *specNch, int *waveNch){ return 0; }
int DummyVSAAdd(void *data, int timestamp){ return 0; }
void DummyVSASetInfo(int srate, int nch){}
void DummySetInfo(int bitrate, int srate, int stereo, int synched){}

WNDPROC lpWndProcOld = NULL, lpWndProc = NULL;

typedef In_Module *(*PluginGetter)();

#define WAVESEEK_TMP_DLL_FILENAME L"waveseek.tmp"

#define TIMER_ID 31337
#define TIMER_FREQ 100

wchar_t szFilename[MAX_PATH] = {0};
wchar_t szDLLPath[MAX_PATH] = {0};
wchar_t szIniPath[MAX_PATH] = {0};
wchar_t szWaveCacheDir[MAX_PATH] = {0};
wchar_t szWaveCacheFile[MAX_PATH] = {0};
wchar_t szTempDLLDestination[MAX_PATH] = {0};

In_Module * pModule = NULL;
HMODULE hDLL = NULL;
int nLengthInMS = 0, no_uninstall = 1, delay_load = -1;
bool bIsPlaying = false, isUnicode = true;

DWORD delay_ipc = -1;

COLORREF clrBackground = RGB(0,0,0);
COLORREF clrWaveform = RGB(0,255,0);
COLORREF clrWaveformPlayed = RGB(0,127,0);

void PluginConfig();

UINT ver = -1;
UINT GetWinampVersion()
{
  if(ver == -1)
  {
    return (ver = (UINT)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETVERSION));
  }
  return ver;
}

void GetFilePaths()
{
  // find the winamp.ini for the Winamp install being used
  if(SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETVERSION) >= 0x5058)
  {
    wcsncpy(szIniPath, (wchar_t *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETINIFILEW), ARRAYSIZE(szIniPath));
    wcsncpy(szWaveCacheDir, (wchar_t *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETINIDIRECTORYW), ARRAYSIZE(szWaveCacheDir));
  }
  else
  {
    // these exist in v2.9x clients but not before
    wcsncpy(szIniPath, AutoWide((char *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETINIFILE)), ARRAYSIZE(szIniPath));
    wcsncpy(szWaveCacheDir, AutoWide((char *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETINIDIRECTORY)), ARRAYSIZE(szWaveCacheDir));
  }

  // make the cache folder in the user's settings folder e.g. %APPDATA%\Winamp\Plugins\wavecache
  // which will better ensure that the cache will be correctly generated though it will fallback
  // to %PROGRAMFILES(x86)%\Winamp\Plugins\wavecache or %PROGRAMFILES%\Winamp\Plugins\wavecache
  // as applicable to the Windows and Winamp version being used (more so with pre v5.11 clients)
  PathAppendW( szWaveCacheDir, L"Plugins\\wavecache" );
  CreateDirectoryW( szWaveCacheDir, NULL );

  // find the correct Winamp\Plugins folder (using native api before making a good guess at it)
  wchar_t *dir = (wchar_t *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORYW);
  if (dir == (wchar_t *)1 || dir == 0)
  {
    char *dirA = (char *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORY);
    if (dirA == (char *)1 || dirA == 0)
    {
      GetModuleFileNameW( pPluginDescription.hDllInstance, szDLLPath, MAX_PATH );
      PathRemoveFileSpecW( szDLLPath );
    }
    else
      lstrcpynW(szDLLPath, AutoWide(dirA), MAX_PATH);
  }
  else
    lstrcpynW(szDLLPath, dir, MAX_PATH);
}

void SaveConfig() 
{
  if (!no_uninstall) return;

  wchar_t sz[32] = {0};
  StringCchPrintfW(sz, 32, L"%d", embed.r.left); WritePrivateProfileStringW(L"Waveseek", L"PosX", sz, szIniPath);
  StringCchPrintfW(sz, 32, L"%d", embed.r.top); WritePrivateProfileStringW(L"Waveseek", L"PosY", sz, szIniPath);
  StringCchPrintfW(sz, 32, L"%d", embed.r.right - embed.r.left); WritePrivateProfileStringW(L"Waveseek", L"SizeX", sz, szIniPath);
  StringCchPrintfW(sz, 32, L"%d", embed.r.bottom - embed.r.top); WritePrivateProfileStringW(L"Waveseek", L"SizeY", sz, szIniPath);
}

// TODO this is not safe where there's no write permissions in the Winamp\Plugins folder
void StartProcessingFile( wchar_t * szFn )
{
  nBufferPointer = 0;
  ZeroMemory(pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short));

  bIsPlaying = false;
  wchar_t * szExt = PathFindExtensionW( szFilename );
  if (!szExt) return;

  nSampleRate = 0;
  pModule = NULL;
  hDLL = NULL;

  wchar_t szDLLMask[MAX_PATH] = {0};
  wcsncpy( szDLLMask, szDLLPath, MAX_PATH );
  PathAppendW( szDLLMask, L"in_*.dll" );

  wcsncpy( szTempDLLDestination, szDLLPath, MAX_PATH );
  PathAppendW( szTempDLLDestination, WAVESEEK_TMP_DLL_FILENAME );
  if (PathFileExistsW(szTempDLLDestination))
  {
    DeleteFileW( szTempDLLDestination );
  }

  WIN32_FIND_DATAW wfd = {0};
  HANDLE hFind = FindFirstFileW( szDLLMask, &wfd );
  if (hFind != INVALID_HANDLE_VALUE)
  {
    bool found = false;
    do {
      HMODULE hDLL = GetModuleHandleW( wfd.cFileName );
      if (hDLL)
      {
        PluginGetter pluginGetter = (PluginGetter)GetProcAddress(hDLL, "winampGetInModule2");
        if (pluginGetter)
        {
          In_Module * pModule = pluginGetter();
          if (pModule)
          {
            char * p = pModule->FileExtensions;
            while(p && *p)
            {
              char * sz = strdup( p );
              if (sz)
              {
                char * pe = strtok( sz, ";" );

                do {
                  if (stricmp( AutoChar(CharNextW(szExt)), pe ) == 0)
                  {
                    wchar_t szSource[MAX_PATH] = {0};
                    wcsncpy( szSource, szDLLPath, MAX_PATH );
                    PathAppendW( szSource, wfd.cFileName );

                    CopyFileW( szSource, szTempDLLDestination, FALSE );
                    found = true;
                    break;
                  }
                } while (pe = strtok( NULL, ";" ));
                free(sz);

                if (found)
                {
                  break;
                }
              }
              p += strlen(p);
              p += strlen(p);
            }
          }
        }
      }
    } while (FindNextFileW(hFind,&wfd));
    FindClose(hFind);
  }

  if (PathFileExistsW(szTempDLLDestination))
  {
    hDLL = LoadLibraryW( szTempDLLDestination );
    if (!hDLL) return;
    PluginGetter pluginGetter = (PluginGetter)GetProcAddress(hDLL, "winampGetInModule2");
    if (!pluginGetter) return;
    pModule = pluginGetter();

    pModule->hMainWindow = hWndWaveseek;
    pModule->hDllInstance = hDLL;

    pModule->outMod = &pDummyOutputPluginDescription;
    pModule->outMod->hMainWindow = hWndWaveseek;
    pModule->outMod->hDllInstance = hDLL;
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
      pModule->service = (api_service *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE);
    }

    pModule->Init();

    nLengthInMS = -1;
    if (pModule->version & IN_UNICODE)
    {
      WCHAR szTitle[GETFILEINFO_TITLE_LENGTH] = {0};
      pModule->GetFileInfo( (char*)szFn, (char*)szTitle, &nLengthInMS );
      if (nLengthInMS <= 0) return;

      int nResult = pModule->Play( (char*)szFn );
      if (nResult != 0) return;
    }
    else
    {
      char szTitle[GETFILEINFO_TITLE_LENGTH] = {0}, szFile[MAX_PATH] = {0};
      strncpy(szFile, AutoChar(szFilename), MAX_PATH);
      pModule->GetFileInfo( szFile, szTitle, &nLengthInMS );
      if (nLengthInMS <= 0) return;

      int nResult = pModule->Play( szFile );
      if (nResult != 0) return;
    }
  }
  else
    return;

  bIsPlaying = true;
}

void FinishProcessingFile()
{
  HANDLE h = CreateFileW( szWaveCacheFile, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, NULL, NULL );
  if (h != INVALID_HANDLE_VALUE)
  {
    DWORD dw = 0;
    WriteFile(h,pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short),&dw,NULL);
    CloseHandle(h);
  }

  if (pModule)
  {
    pModule->Stop();
    pModule->Quit();
    pModule = NULL;
  }

  if (hDLL)
  {
    FreeLibrary(hDLL);
    hDLL = NULL;
  }
}

void ProcessStop()
{
  if (pModule)
  {
    if (bIsPlaying)
      pModule->Stop();
    if (hDLL)
    {
      FreeLibrary(hDLL);
      hDLL = NULL;
    }
  }
  bIsPlaying = false;
}

typedef struct {
  char szPerformer[256];
  char szTitle[256];
  unsigned int nMillisec;
} CUETRACK;

int nCueTracks = 0;
CUETRACK pCueTracks[256];

void LoadCUE( wchar_t * szFn )
{
  FILE * f = _wfopen( szFn, L"rt" );
  if (!f)
    return;

  nCueTracks = 0;
  int nCurrentTrack = 0;
  char strs[256];
  while(fgets(strs,256,f))
  {
    char * str = strs;
    while(isspace(*str)) str++;

    if (strstr(str,"TRACK") == str)
    {
      sscanf( str, "TRACK %d AUDIO", &nCurrentTrack );
      nCurrentTrack = min(nCurrentTrack, 255);
      nCueTracks = max(nCueTracks, nCurrentTrack);
      pCueTracks[ nCurrentTrack - 1 ].szPerformer[0] = 0;
      pCueTracks[ nCurrentTrack - 1 ].szTitle[0] = 0;
    }
    if (nCurrentTrack > 0)
    {
      CUETRACK & track = pCueTracks[ nCurrentTrack - 1 ];
      if (strstr(str,"PERFORMER") == str)
      {
        sscanf( str, "PERFORMER \"%[^\"]\"", track.szPerformer );
      }
      if (strstr(str,"TITLE") == str)
      {
        sscanf( str, "TITLE \"%[^\"]\"", track.szTitle );
      }
      if (strstr(str,"INDEX") == str)
      {
        int m=0,s=0,f=0;
        sscanf( str, "INDEX %*d %d:%d:%d", &m,&s,&f );
        track.nMillisec = m * 60 * 1000 + s * 1000 + (f * 1000) / 75;
      }
    }
  }
  fclose(f);
}

void ProcessFilePlayback( wchar_t * szFn )
{
  ProcessStop();

  wchar_t szCue[MAX_PATH];
  wcsncpy(szCue,szFn,MAX_PATH);
  wchar_t * szExt = wcsrchr(szCue,'.');
  if (szExt)
    wcsncpy(szExt,L".cue",MAX_PATH - (szExt - szCue));

  nCueTracks = 0;
  if (PathFileExistsW(szCue))
    LoadCUE(szCue);

  nBufferPointer = 0;
  ZeroMemory(pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short));

  if (szFn && *szFn)
  {
    wcsncpy(szFilename, szFn, MAX_PATH);
    wcsncpy( szWaveCacheFile, szWaveCacheDir, MAX_PATH );
    PathAppendW( szWaveCacheFile, PathFindFileNameW( szFilename ) );
    wcsncat( szWaveCacheFile, L".cache", MAX_PATH );
  }

  if (!PathFileExistsW( szWaveCacheFile ))
  {
    StartProcessingFile( szFilename );
  }
  else
  {
    HANDLE h = CreateFileW( szWaveCacheFile, GENERIC_READ, NULL, NULL, OPEN_EXISTING, NULL, NULL );
    if (h != INVALID_HANDLE_VALUE)
    {
      DWORD dw = 0;
      ReadFile(h,pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short),&dw,NULL);
      CloseHandle(h);
    }
  }
}

void ProcessSkinChange()
{
  WADlg_init(pPluginDescription.hwndParent);

  WCHAR szBuffer[MAX_PATH] = {0};
  SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, (WPARAM)szBuffer, IPC_GETSKINW);

  clrBackground = WADlg_getColor(WADLG_ITEMBG);
  clrWaveform = WADlg_getColor(WADLG_ITEMFG);

  if (GetWinampVersion() >= 0x5066)
  {
    clrWaveformPlayed = (szBuffer[0] ? (COLORREF)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 4, IPC_GET_GENSKINBITMAP) : RGB(0,127,0));
  }
  else
  {
    if (szBuffer[0])
    {
      WCHAR sz[20] = {0};
      unsigned int r = 0, g = 0, b = 0;

      PathAppendW(szBuffer, L"pledit.txt");
      GetPrivateProfileStringW(L"Text", L"Current", L"#008000", sz, 20, szBuffer);
      swscanf(sz,L"#%2X%2X%2X",&r,&g,&b);
      clrWaveformPlayed = RGB(r,g,b);
    }
    else
    {
      clrWaveformPlayed = RGB(0,127,0);
    }
  }

  if (abs(GetColorDistance(clrWaveformPlayed, clrWaveform)) < 70 || !szBuffer[0])
  {
    clrWaveformPlayed = BlendColors(clrBackground, clrWaveform, (COLORREF)77);
  }
}

void PaintWaveform(HDC hdc, RECT rc)
{
  int nSongPos = (int)SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME );
  int nSongLen = (int)SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME );

  int nBufPos = nSongLen != -1 ? MulDiv(nSongPos,SAMPLE_BUFFER_SIZE,nSongLen) : 0;

  HDC hdcMem = CreateCompatibleDC( hdc );
  HBITMAP hbmMem = CreateCompatibleBitmap( hdc, rc.right - rc.left, rc.bottom - rc.top );
  HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

  HBRUSH hbrBackground = CreateSolidBrush(clrBackground);
  FillRect(hdcMem,&rc,hbrBackground);

  SelectObject(hdcMem, GetStockObject(DC_PEN));
  int w = (rc.right - rc.left);
  for (int i=0; i<w; i++)
  {
    int nBufLoc0 = (i + 0) * SAMPLE_BUFFER_SIZE / w;
    int nBufLoc1 = (i + 1) * SAMPLE_BUFFER_SIZE / w;
    nBufLoc1 = min(nBufLoc1,SAMPLE_BUFFER_SIZE);
    if (nBufLoc0 < nBufPos)
    SetDCPenColor(hdcMem, clrWaveformPlayed);
    else
    SetDCPenColor(hdcMem, clrWaveform);

    unsigned short nSample = 0;
    for (int j=nBufLoc0; j<nBufLoc1; j++)
    nSample = max(pSampleBuffer[ j ],nSample);

    int h = (rc.bottom - rc.top);
    unsigned short sh = nSample * h / 32767;
    MoveToEx(hdcMem,i,(h - sh) / 2,NULL);
    LineTo(hdcMem,i,(h - sh) / 2 + sh);
  }
  BitBlt( hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hdcMem, 0, 0, SRCCOPY );

  DeleteObject(hbrBackground);

  SelectObject(hdcMem, hbmOld);
  DeleteObject(hbmMem);
  DeleteDC(hdcMem);
}

HWND GetMLHwnd(void)
{
  static HWND mlwnd = NULL;
  if (!IsWindow(mlwnd))
  {
    static LONG_PTR libhwndipc;
    if (!libhwndipc || libhwndipc <= 65535)
    {
      libhwndipc = (LONG_PTR)SendMessage(pPluginDescription.hwndParent,WM_WA_IPC,(WPARAM)&"LibraryGetWnd",IPC_REGISTER_WINAMP_IPCMESSAGE);
    }

    mlwnd = (HWND)SendMessage(pPluginDescription.hwndParent,WM_WA_IPC,-1,(LPARAM)libhwndipc);
  }
  return mlwnd;
}

INT Menu_TrackPopup(HMENU hMenu, UINT fuFlags, INT x, INT y,  HWND hwnd, LPTPMPARAMS lptpm, BOOL ml_skinned){
  if(hMenu == NULL){
    return NULL;
  }

  if(ml_skinned && IsWindow(GetMLHwnd()))
  {
    MLSKINNEDPOPUP popup = {0};
    popup.cbSize = sizeof(MLSKINNEDPOPUP);
    popup.hmenu = hMenu;
    popup.fuFlags = fuFlags;
    popup.x = x;
    popup.y = y;
    popup.hwnd = hwnd;
    popup.lptpm = lptpm;
    popup.skinStyle = SMS_USESKINFONT;
    return (INT)SENDMLIPC(GetMLHwnd(), ML_IPC_TRACKSKINNEDPOPUPEX, &popup);
  }
  else
  {
    return TrackPopupMenuEx(hMenu, fuFlags, x, y, hwnd, lptpm);
  }
}

TOOLINFO ti = {0};
INT_PTR CALLBACK EmdedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  //   WCHAR sz[1024] = {0};
  //   StringCchPrintfW(sz,1024,L"uMsg: %8d - wParam: %08X - lParam: %08X\n",uMsg,wParam,lParam);
  //   OutputDebugStringW(sz);

  switch (uMsg)
  {
    case WM_WA_MPEG_EOF:
      {
        FinishProcessingFile();
      } break;
    case WM_WA_IPC:
      {
        // this is used to pass on some of the messages but not all
        // since we use this window as a fake 'main' window so some
        // of the messages like WM_WA_MPEG_EOF are blocked from the
        // real main window when using the dll re-use hack in place
      } return SendMessage(pPluginDescription.hwndParent,uMsg,wParam,lParam);
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
      {
        PostMessage(pPluginDescription.hwndParent,uMsg,wParam,lParam);
      } break;
    case WM_CONTEXTMENU:
      {
        short xPos = GET_X_LPARAM(lParam);
        short yPos = GET_Y_LPARAM(lParam);

        static HMENU hMenu = NULL;
        if (!hMenu)
        {
          hMenu = LoadMenuW( pPluginDescription.hDllInstance, MAKEINTRESOURCEW(IDR_CONTEXTMENU) );
        }
        HMENU hSubMenu = GetSubMenu( hMenu, 0 );

        // this will handle the menu being shown not via the mouse actions
        // so is positioned just below the header if no selection but there's a queue
        // or below the item selected (or the no files in queue entry)
        if (xPos == -1 || yPos == -1)
        {
          RECT rc;
          GetWindowRect(GetWindow(hWnd, GW_CHILD), &rc);
          xPos = (short)rc.left;
          yPos = (short)rc.top;
        }

        switch(LOWORD(Menu_TrackPopup(hSubMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD, xPos, yPos, hWnd, 0, 1)))
        {
          case ID_SUBMENU_RERENDER:
            {
              StartProcessingFile( szFilename );
            } break;
          case ID_SUBMENU_ABOUT:
            {
              PluginConfig();
            } break;
        }
      } break;
  }
  return CallWindowProcW(lpWndProc, hWnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK InnerWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  //   WCHAR sz[1024] = {0};
  //   StringCchPrintfW(sz,1024,L"uMsg: %8d - wParam: %08X - lParam: %08X\n",uMsg,wParam,lParam);
  //   OutputDebugStringW(sz);

  switch (uMsg)
  {
    case WM_TIMER:
      {
        if (wParam == TIMER_ID)
        {
          InvalidateRect(hWnd,NULL,TRUE);
        }
      } break;
    case WM_ERASEBKGND:
      {
        RECT rc;
        GetClientRect(hWnd,&rc);
        PaintWaveform((HDC)wParam, rc);
      } return 1;
    case WM_PAINT:
      {
        RECT rc;
        GetClientRect(hWnd,&rc);
        PAINTSTRUCT ps = {0};
        BeginPaint(hWnd, &ps);
        PaintWaveform(ps.hdc, rc);
        EndPaint(hWnd, &ps);
      } break;
    case WM_LBUTTONUP:
      {
        int nSongLen = (int)SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME );
        if (nSongLen != -1)
        {
          RECT rc;
          GetClientRect(hWnd,&rc);
          unsigned int ms = MulDiv(GET_X_LPARAM(lParam),nSongLen,(rc.right - rc.left));
          SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, ms, IPC_JUMPTOTIME);
        }
      } break;
    case WM_MOUSEMOVE:
      {
        static short xOldPos = 0; 
        static short yOldPos = 0; 
        short xPos = GET_X_LPARAM(lParam); 
        short yPos = GET_Y_LPARAM(lParam); 
        if (xPos == xOldPos && yPos == yOldPos)
          break;

        xOldPos = xPos;
        yOldPos = yPos;

        nLengthInMS = (int)SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME );
        if (nLengthInMS != -1)
        {
          RECT rc;
          GetClientRect(hWnd,&rc);
          unsigned int ms = MulDiv(xPos,nLengthInMS,(rc.right - rc.left));
          unsigned int sec = ms / 1000;

          // ensures we'll get a WM_MOUSELEAVE 
          TRACKMOUSEEVENT trackMouse = {0};
          trackMouse.cbSize = sizeof(trackMouse);
          trackMouse.dwFlags = TME_LEAVE;
          trackMouse.hwndTrack = hWnd;
          TrackMouseEvent(&trackMouse);

          TCHAR coords[128] = {0};

          int nTrack = -1;
          for (int i=0; i<nCueTracks; i++)
          {
            if ( i < nCueTracks - 1 )
            {
              if ( pCueTracks[i].nMillisec < ms && ms < pCueTracks[i + 1].nMillisec )
                nTrack = i;
            }
            else
            {
              if (pCueTracks[i].nMillisec < ms )
                nTrack = i;
            }
          }
          if (nTrack >= 0)
          {
            if (strlen(pCueTracks[nTrack].szPerformer))
              StringCchPrintf(coords, 128, _T("[%d:%02d] %s - %s"), sec / 60, sec % 60, pCueTracks[nTrack].szPerformer, pCueTracks[nTrack].szTitle);
            else
              StringCchPrintf(coords, 128, _T("[%d:%02d] %s"), sec / 60, sec % 60, pCueTracks[nTrack].szTitle);
          }
          else
            StringCchPrintf(coords, 128, _T("%d:%02d"), sec / 60, sec % 60);
          ti.lpszText = coords;

          POINT pt = { xPos, yPos }; 
          ClientToScreen(hWndWaveseek, &pt);

          SendMessage( hWndToolTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti );
          SendMessage( hWndToolTip, TTM_SETTOOLINFO, 0, (LPARAM)&ti);
          SendMessage( hWndToolTip, TTM_SETMAXTIPWIDTH, 0, 320 );
          SendMessage( hWndToolTip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x + 15, pt.y - 10));
        }
      } break;
    case WM_MOUSELEAVE:
      {
        SendMessage( hWndToolTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti );
      } break;
  }
  return 0;
}

LRESULT CALLBACK WinampHookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_DISPLAYCHANGE:
    {
      ProcessSkinChange();
    }
    break;
  case WM_WA_IPC:
    {
      //       WCHAR sz[1024] = {0};
      //       StringCchPrintfW(sz,1024,L"IPC: uMsg: %8d - wParam: %08X - lParam: %08X\n",uMsg,wParam,lParam);
      //       OutputDebugStringW(sz);

      // this is used on 5.3+ clients
      if(lParam == IPC_PLAYING_FILEW)
      {
        ProcessFilePlayback((wchar_t*) wParam);
      }
      else if(lParam == IPC_PLAYING_FILE)
      {
        // only need to use this callback on pre 5.3 clients
        if (GetWinampVersion() < 0x5030)
        {
          wchar_t * szFn = AutoWideDup((char*) wParam);
          if (szFn)
          {
            ProcessFilePlayback(szFn);
            free(szFn);
          }
        }
      }
      // make sure we catch all appropriate skin changes
      else if (lParam == IPC_SKIN_CHANGED ||
          lParam == IPC_CB_RESETFONT ||
          lParam == IPC_FF_ONCOLORTHEMECHANGED)
      {
        ProcessSkinChange();
      }
      else if (lParam == delay_load)
      {
        // just incase we need to handle a migration update, we check for
        // a %PROGRAMFILES(x86)%\Winamp\Plugins\wavecache folder and if
        // applicable then we move it into the correct settings folder
        if (GetWinampVersion() >= 0x5011)
        {
          if (!GetPrivateProfileIntW(L"Waveseek", L"Migrate", 0, szIniPath))
          {
            wchar_t szOldWaveCacheDir[MAX_PATH] = {0};
            PathCombineW(szOldWaveCacheDir, szDLLPath, L"wavecache");
            if (PathFileExistsW(szOldWaveCacheDir))
            {
              wchar_t szFnFind[MAX_PATH] = {0};
              PathCombineW(szFnFind, szOldWaveCacheDir, L"*.cache");

              WIN32_FIND_DATAW wfd = {0};
              HANDLE hFind = FindFirstFileW( szFnFind, &wfd );
              if (hFind != INVALID_HANDLE_VALUE)
              {
                do
                {
                  // if we found a *.cache file then move it over
                  // as long as there's permission and the OS can
                  wchar_t szFnMove[MAX_PATH] = {0};
                  PathCombineW(szFnFind, szOldWaveCacheDir, wfd.cFileName);
                  PathCombineW(szFnMove, szWaveCacheDir, wfd.cFileName);
                  MoveFileExW(szFnFind, szFnMove, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
                } while (FindNextFileW(hFind,&wfd));
                CloseHandle(hFind);
              }
            }
            WritePrivateProfileStringW(L"Waveseek", L"Migrate", L"1", szIniPath);
          }
        }
        ProcessFilePlayback( (wchar_t *)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS), IPC_GETPLAYLISTFILEW) );
      }
    } break;
  }

  if (isUnicode)
    return CallWindowProcW(lpWndProcOld,hWnd,uMsg,wParam,lParam);
  else
    return CallWindowProcA(lpWndProcOld,hWnd,uMsg,wParam,lParam);
}

int PluginInit() 
{
  GetFilePaths();

  ProcessSkinChange();

  RECT rc;
  GetWindowRect(pPluginDescription.hwndParent,&rc);

  embed.r.left = GetPrivateProfileIntW(L"Waveseek", L"PosX", rc.left, szIniPath);
  embed.r.top = GetPrivateProfileIntW(L"Waveseek", L"PosY", rc.top - (rc.bottom - rc.top), szIniPath);
  embed.r.right = embed.r.left + GetPrivateProfileIntW(L"Waveseek", L"SizeX", 825, szIniPath);
  embed.r.bottom = embed.r.top + GetPrivateProfileIntW(L"Waveseek", L"SizeY", 116, szIniPath);
  SET_EMBED_GUID((&embed), embed_guid);

  hWndWaveseek = (HWND)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, (WPARAM)&embed, IPC_GET_EMBEDIF);
  lpWndProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrW(hWndWaveseek, GWLP_WNDPROC, (LONGX86)(LONG_PTR)EmdedWndProc);
  HWND hWndWaveseek2 = CreateDialogW(pPluginDescription.hDllInstance, MAKEINTRESOURCEW(IDD_VIEW), hWndWaveseek, InnerWndProc);

  SetWindowTextW(hWndWaveseek, L"Waveform Seeker");
  ShowWindow(hWndWaveseek, SW_SHOW);
  SetTimer( hWndWaveseek2, TIMER_ID, TIMER_FREQ, NULL );

  hWndToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                  WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                  CW_USEDEFAULT, hWndWaveseek, NULL,
                  pPluginDescription.hDllInstance, NULL);

  ti.cbSize = sizeof(TOOLINFO);
  ti.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
  ti.hwnd = hWndWaveseek;
  ti.hinst = pPluginDescription.hDllInstance;
  ti.uId = (UINT_PTR)hWndWaveseek;
  SendMessage( hWndToolTip, TTM_ADDTOOL, NULL, (LPARAM)&ti);

  if (IsWindowUnicode(pPluginDescription.hwndParent))
  {
    lpWndProcOld = (WNDPROC)(LONG_PTR)SetWindowLongPtrW(pPluginDescription.hwndParent,GWLP_WNDPROC,(LONGX86)(LONG_PTR)WinampHookWndProc);
  }
  else
  {
    lpWndProcOld = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(pPluginDescription.hwndParent,GWLP_WNDPROC,(LONGX86)(LONG_PTR)WinampHookWndProc);
    isUnicode = false;
  }

  // restore / process the current file so we're showing something on load
  // but we delay it a bit until Winamp is in a better state especially if
  // we then fire offa file processing action otherwise we slow down startup
  delay_load = (int)SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, (WPARAM)&"wave_seeker", IPC_REGISTER_WINAMP_IPCMESSAGE);
  PostMessage(pPluginDescription.hwndParent, WM_WA_IPC, 0, delay_load);

  return 0;
}

void PluginConfig()
{
  MessageBox( pPluginDescription.hwndParent, 
  _T("Waveform Seeker version ") _T( __DATE__ ) _T("\n")
  _T("by Gargaj\n")
  _T("\n")
  _T("Updated versions and source available at:\n")
  _T("http://code.google.com/p/gen-waveseek/\n"),
  _T("Waveform Seeker"), MB_ICONINFORMATION);
}

void PluginQuit()
{
  ProcessStop();
  SaveConfig();

  KillTimer( hWndWaveseek, TIMER_ID );
  DestroyWindow( hWndToolTip );
  DestroyWindow( hWndWaveseek );

  if (PathFileExistsW(szTempDLLDestination))
  {
    DeleteFileW( szTempDLLDestination );
  }
}

winampGeneralPurposePlugin pPluginDescription =
{
  GPPHDR_VER,
  "Waveform Seeker",
  PluginInit,
  PluginConfig,
  PluginQuit,
  NULL,
  NULL,
};

#ifdef __cplusplus
extern "C" {
#endif
   __declspec( dllexport ) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin()
  {
    return &pPluginDescription;
  }

  __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param){
    // prompt to remove our settings with default as no (just incase)
    if(MessageBoxA(hwndDlg, "Do you also want to remove the saved settings for this plug-in?",
            pPluginDescription.description,MB_YESNO|MB_DEFBUTTON2) == IDYES){
      WritePrivateProfileStringW(L"Waveseek", 0, 0, szIniPath);
      no_uninstall = 0;
    }

    // as we're doing too much in subclasses, etc we cannot allow for on-the-fly removal so need to do a normal reboot
    return GEN_PLUGIN_UNINSTALL_REBOOT;
  }

#ifdef __cplusplus
}
#endif