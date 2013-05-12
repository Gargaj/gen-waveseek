#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <Shlwapi.h>
#include <winamp/IN2.H>
#include <winamp/gen.h>
#include <winamp/wa_ipc.h>
#include "gen_waveseek.h"

#define WA_DLG_IMPLEMENT
#include <winamp/wa_dlg.h>

HWND hWndWaveseek = NULL;

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

WNDPROC lpWndProcOld = NULL;

typedef In_Module *(*PluginGetter)();

#define WAVESEEK_TMP_DLL_FILENAME "waveseek.tmp"

#define TIMER_ID 31337
#define TIMER_FREQ 100

char szFilename[MAX_PATH] = {0};
char szDLLPath[MAX_PATH] = {0};
char szWaveCacheFile[MAX_PATH] = {0};
In_Module * pModule = NULL;
HMODULE hDLL = NULL;
int nLengthInMS = 0;
bool bIsPlaying = false;

HBITMAP bmpSkin = NULL;

COLORREF clrBackground = RGB(0,0,0);
COLORREF clrWaveform = RGB(0,255,0);
COLORREF clrWaveformPlayed = RGB(0,127,0);

void StartProcessingFile( char * szFn )
{
  bIsPlaying = false;
  strncpy(szFilename,szFn,MAX_PATH);
  char * szExt = PathFindExtensionA( szFilename );
  if (!szExt) return;

  nSampleRate = 0;
  pModule = NULL;
  hDLL = NULL;

  char szDLLMask[MAX_PATH];
  strcpy( szDLLMask, szDLLPath );
  PathAppendA( szDLLMask, "in_*.dll" );

  char szTempDLLDestination[MAX_PATH];
  strcpy( szTempDLLDestination, szDLLPath );
  PathAppendA( szTempDLLDestination, WAVESEEK_TMP_DLL_FILENAME );

  DeleteFileA( WAVESEEK_TMP_DLL_FILENAME );

  WIN32_FIND_DATAA wfd;
  ZeroMemory(&wfd,sizeof(WIN32_FIND_DATAA));
  HANDLE hFind = FindFirstFileA( szDLLMask, &wfd );
  if (hFind) do {
    HMODULE hDLL = LoadLibraryA( wfd.cFileName );
    if (hDLL)
    {
      PluginGetter pluginGetter = (PluginGetter)GetProcAddress(hDLL, "winampGetInModule2");
      if (pluginGetter)
      {
        In_Module * pModule = pluginGetter();
        if (pModule && pModule->is_seekable)
        {
          char * p = strdup(pModule->FileExtensions);
          while(*p)
          {
            char * sz = strdup( p );
            char * pe = strtok( sz, ";" );
            BOOL b = false;
            do {
              if (stricmp( szExt + 1, pe ) == 0)
              {
                char szSource[MAX_PATH];
                strcpy( szSource, szDLLPath );
                PathAppendA( szSource, wfd.cFileName );

                b = CopyFileA( szSource, szTempDLLDestination, FALSE );
              }
            } while (pe = strtok( NULL, ";" ));
            free(sz);
            p += strlen(p);
            p += strlen(p);
          }
        }
      }
      FreeLibrary(hDLL);
    }
  } while (FindNextFileA(hFind,&wfd));

  hDLL = LoadLibraryA( szTempDLLDestination );
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

  pModule->Init();

  nLengthInMS = -1;
  if (pModule->version & IN_UNICODE)
  {
    WCHAR sz[MAX_PATH];
    ZeroMemory(sz,MAX_PATH * sizeof(WCHAR));
    MultiByteToWideChar( CP_OEMCP, NULL, szFilename, -1, sz, MAX_PATH - 1);

    WCHAR szTitle[GETFILEINFO_TITLE_LENGTH];
    pModule->GetFileInfo( (char*)sz, (char*)szTitle, &nLengthInMS );
//    if (!pModule->IsOurFile( (char*)sz )) return;
    if (nLengthInMS <= 0) return;

    int nResult = pModule->Play( (char*)sz );
    if (nResult != 0) return;
  }
  else
  {
    char szTitle[GETFILEINFO_TITLE_LENGTH];
    pModule->GetFileInfo( szFilename, szTitle, &nLengthInMS );
//    if (!pModule->IsOurFile( (char*)szFilename )) return;
    if (nLengthInMS <= 0) return;

    int nResult = pModule->Play( szFilename );
    if (nResult != 0) return;
  }
  bIsPlaying = true;
}

void FinishProcessingFile()
{
  HANDLE h = CreateFileA( szWaveCacheFile, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, NULL, NULL );
  if (h != INVALID_HANDLE_VALUE)
  {
    DWORD dw = 0;
    WriteFile(h,pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short),&dw,NULL);
    CloseHandle(h);
  }

  pModule->Stop();

  pModule->Quit();

  pModule = NULL;

  FreeLibrary(hDLL);
  hDLL = NULL;
}

void ProcessSkinChange()
{
  if (bmpSkin)
  {
    DeleteObject(bmpSkin);
  }

  WCHAR szBuffer[MAX_PATH];
  SendMessage(pPluginDescription.hwndParent, WM_WA_IPC, (WPARAM)szBuffer, IPC_GETSKINW);

  HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
  if (wcslen(szBuffer))
  {
    WCHAR szBufferFile[MAX_PATH];
    CopyMemory(szBufferFile,szBuffer,sizeof(WCHAR) * MAX_PATH);
    PathAppendW(szBufferFile,L"gen.bmp");
    bmpSkin = (HBITMAP)LoadImageW(NULL,szBufferFile,IMAGE_BITMAP,NULL,NULL,LR_LOADFROMFILE);

    CopyMemory(szBufferFile,szBuffer,sizeof(WCHAR) * MAX_PATH);
    PathAppendW(szBufferFile,L"pledit.txt");

    WCHAR sz[20];
    unsigned int r = 0, g = 0, b = 0;
    GetPrivateProfileStringW(L"Text",L"NormalBG",L"#000000",sz,20,szBufferFile);
    swscanf(sz,L"#%2X%2X%2X",&r,&g,&b);
    clrBackground = RGB(r,g,b);

    GetPrivateProfileStringW(L"Text",L"Normal",L"#00ff00",sz,20,szBufferFile);
    swscanf(sz,L"#%2X%2X%2X",&r,&g,&b);
    clrWaveform = RGB(r,g,b);

    GetPrivateProfileStringW(L"Text",L"Current",L"#008000",sz,20,szBufferFile);
    swscanf(sz,L"#%2X%2X%2X",&r,&g,&b);
    clrWaveformPlayed = RGB(r,g,b);

  }
  else
  {
    clrBackground = RGB(0,0,0);
    clrWaveform = RGB(0,255,0);
    clrWaveformPlayed = RGB(0,127,0);

    bmpSkin = LoadBitmap(hInstance,MAKEINTRESOURCE(250));
  }
}

LRESULT CALLBACK WinampHookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_WA_IPC:
    {
      if(lParam == IPC_PLAYING_FILE)
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

        nBufferPointer = 0;
        ZeroMemory(pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short));

        char * szFilename = (char*) wParam;
        GetModuleFileNameA( pPluginDescription.hDllInstance, szDLLPath, MAX_PATH );
        PathRemoveFileSpecA( szDLLPath );

        char szWaveCacheDir[MAX_PATH];
        strcpy( szWaveCacheDir, szDLLPath );
        PathAppendA( szWaveCacheDir, "wavecache" );
        CreateDirectoryA( szWaveCacheDir, NULL );

        strcpy( szWaveCacheFile, szWaveCacheDir );
        PathAppendA( szWaveCacheFile, PathFindFileNameA( szFilename ) );
        strcat( szWaveCacheFile, ".cache" );

        if (!PathFileExistsA( szWaveCacheFile ))
        {
          StartProcessingFile( szFilename );
        }
        else
        {
          HANDLE h = CreateFileA( szWaveCacheFile, GENERIC_READ, NULL, NULL, OPEN_EXISTING, NULL, NULL );
          if (h != INVALID_HANDLE_VALUE)
          {
            DWORD dw = 0;
            ReadFile(h,pSampleBuffer,SAMPLE_BUFFER_SIZE * sizeof(unsigned short),&dw,NULL);
            CloseHandle(h);
          }
        }

      } 
      else if (lParam == IPC_SKIN_CHANGED)
      {
        ProcessSkinChange();
      }
    } break;
  case WM_MOVE:
    {
      unsigned short xPos = (int)(short) LOWORD(lParam);   // horizontal position 
      unsigned short yPos = (int)(short) HIWORD(lParam);   // vertical position 

      SetWindowPos( hWndWaveseek, NULL, xPos, yPos - 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    } break;
  }
  return CallWindowProc(lpWndProcOld,hWnd,uMsg,wParam,lParam);
}

LRESULT CALLBACK BoxWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
//   WCHAR sz[1024];
//   _snwprintf(sz,1024,L"uMsg: %8d - wParam: %08X - lParam: %08X\n",uMsg,wParam,lParam);
//   OutputDebugStringW(sz);
  RECT rc;
  GetClientRect(hWnd,&rc);

  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;

  int nInnerX = 11;
  int nInnerY = 20;
  int nInnerW = w - 11 - 8;
  int nInnerH = h - 20 - 14;

  // this makes it look less brickwalled
  nInnerY += 2; 
  nInnerH -= 4;

  switch (uMsg)
  {
    case WM_TIMER:
      {
        if (wParam == TIMER_ID)
        {
          InvalidateRect(hWnd,NULL,FALSE);
          //SetTimer( hWndWaveseek, TIMER_ID, TIMER_FREQ, NULL );
        }
      } break;
    case WM_WA_MPEG_EOF:
      {
        FinishProcessingFile();
      } break;
    case WM_WA_IPC:
      {
        return CallWindowProc(lpWndProcOld,pPluginDescription.hwndParent,uMsg,wParam,lParam);
      } break;
    case WM_PAINT:
      {
        int nSongPos = SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME );
        int nSongLen = SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME );

        int nBufPos = nSongLen != -1 ? MulDiv(nSongPos,SAMPLE_BUFFER_SIZE,nSongLen) : 0;

        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);

        HDC hdcMem = CreateCompatibleDC( ps.hdc );
        HBITMAP hbmMem = CreateCompatibleBitmap( ps.hdc, rc.right - rc.left, rc.bottom - rc.top );
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        HDC hdcSkin = CreateCompatibleDC( NULL );
        HBITMAP hbmSkinOld = (HBITMAP)SelectObject( hdcSkin, bmpSkin );

        FillRect(hdcMem,&rc,CreateSolidBrush(clrBackground));

        int y = true ? 0 : 26;
        BitBlt( hdcMem, 0, 0, 25, 20, hdcSkin, 0, y, SRCCOPY );
        for (int i=1; i < (w / 25) + 1; i++)
          BitBlt( hdcMem, i * 25, 0, 25, 20, hdcSkin, 104, y, SRCCOPY );
        BitBlt( hdcMem, w - 25, 0, 25, 20, hdcSkin, 130, y, SRCCOPY );

        for (int i=0; i < (h / 29) + 1; i++)
        {
          BitBlt( hdcMem, 0, 20 + i * 29, 11, 29, hdcSkin, 127, 42, SRCCOPY );
          BitBlt( hdcMem, w - 8, 20 + i * 29, 8, 29, hdcSkin, 139, 42, SRCCOPY );
        }

        BitBlt( hdcMem, 0, h - 14, 125, 14, hdcSkin, 0, 42, SRCCOPY );
        for (int i=0; i < (w / 25) + 1; i++)
          BitBlt( hdcMem, 125 + i * 25, h - 14, 25, 14, hdcSkin, 127, 72, SRCCOPY );
        BitBlt( hdcMem, w - 125, h - 14, 125, 14, hdcSkin, 0, 57, SRCCOPY );

        SelectObject(hdcMem, GetStockObject(DC_PEN));
        for (int i=0; i<nInnerW; i++)
        {
          COLORREF c = 0;
          int nBufLoc = i * SAMPLE_BUFFER_SIZE / nInnerW;
          if (nBufLoc < nBufPos)
            c = SetDCPenColor(hdcMem, clrWaveformPlayed);
          else
            c = SetDCPenColor(hdcMem, clrWaveform);
          unsigned short nSample = pSampleBuffer[ nBufLoc ];
          unsigned short sh = nSample * nInnerH / 32767;
          MoveToEx(hdcMem,nInnerX + i,nInnerY + (nInnerH - sh) / 2,NULL);
          LineTo(hdcMem,nInnerX + i,nInnerY + (nInnerH - sh) / 2 + sh);
        }
        BitBlt( ps.hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hdcMem, 0, 0, SRCCOPY );

        SelectObject(hdcSkin, hbmSkinOld);
        DeleteDC(hdcSkin);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);


        EndPaint(hWnd, &ps);
      } break;
    case WM_LBUTTONUP:
      {
        unsigned short xPos = GET_X_LPARAM(lParam); 
        unsigned short yPos = GET_Y_LPARAM(lParam); 

        int nSongLen = SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME );

        if (nSongLen != -1)
        {
          unsigned int ms = MulDiv(xPos - nInnerX,nSongLen,nInnerW);
          SendMessage( pPluginDescription.hwndParent, WM_WA_IPC, ms, IPC_JUMPTOTIME);
        }

      } break;
    case WM_LBUTTONDOWN:
      {
        unsigned short xPos = GET_X_LPARAM(lParam); 
        unsigned short yPos = GET_Y_LPARAM(lParam); 

        if (yPos < 20)
          SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, NULL);
        else if (xPos > w - 15 && yPos > h - 15)
          SendMessage(hWnd, WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, NULL);
      } break;
  }
  return DefWindowProc(hWnd,uMsg,wParam,lParam);
}


int PluginInit() 
{
  if (IsWindowUnicode(pPluginDescription.hwndParent))
    lpWndProcOld = (WNDPROC)SetWindowLongW(pPluginDescription.hwndParent,GWL_WNDPROC,(LONG)WinampHookWndProc);
  else
    lpWndProcOld = (WNDPROC)SetWindowLongA(pPluginDescription.hwndParent,GWL_WNDPROC,(LONG)WinampHookWndProc);

  WNDCLASSA WC;
  WC.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  WC.lpfnWndProc = &BoxWndProc;
  WC.cbClsExtra = 0;
  WC.cbWndExtra = 0;
  WC.hInstance = pPluginDescription.hDllInstance;
  WC.hIcon = NULL;
  WC.hCursor = LoadCursor(NULL, IDC_ARROW);
  WC.hbrBackground = NULL;
  WC.lpszMenuName = NULL;
  WC.lpszClassName = "waveseekwindow";
  RegisterClassA(&WC);

  RECT rc;
  GetWindowRect(pPluginDescription.hwndParent,&rc);
  hWndWaveseek = CreateWindowExA(WS_EX_TOOLWINDOW,"waveseekwindow","MyWindow", WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_POPUP,rc.left,rc.top - 100,825,100,pPluginDescription.hwndParent,0,pPluginDescription.hDllInstance,0);
  SetTimer( hWndWaveseek, TIMER_ID, TIMER_FREQ, NULL );

  ProcessSkinChange();

  return 0;
}

void PluginConfig()
{
}

void PluginQuit()
{
  KillTimer( hWndWaveseek, TIMER_ID );
  DeleteObject( bmpSkin );
  DestroyWindow( hWndWaveseek );
  UnregisterClassA( "waveseekwindow", pPluginDescription.hDllInstance );
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

extern "C" __declspec( dllexport ) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin()
{
  return &pPluginDescription;
}