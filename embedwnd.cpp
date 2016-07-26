#include <windows.h>
#include <strsafe.h>
#include "embedwnd.h"

// internal variables
HMENU main_menu = 0, windows_menu = 0;
int height = 0, width = 0;
BOOL visible = FALSE, old_visible = FALSE;
RECT initial[2] = {0};

HWND CreateEmbeddedWindow(embedWindowState* embedWindow, const GUID embedWindowGUID)
{
	// this sets a GUID which can be used in a modern skin / other parts of Winamp to
	// indentify the embedded window frame such as allowing it to activated in a skin
	SET_EMBED_GUID((embedWindow), embedWindowGUID);

	// when creating the frame it is easier to use Winamp's handling to specify the
	// position of the embedded window when it is created saving addtional handling
	//
	// how you store the settings is down to you, this example uses winamp.ini for ease
	embedWindow->r.left = GetPrivateProfileInt(INI_FILE_SECTION, L"PosX", 275, ini_file);
	embedWindow->r.top = GetPrivateProfileInt(INI_FILE_SECTION, L"PosY", 406, ini_file);
	
	//TODO map from the old values?
	int right = GetPrivateProfileInt(INI_FILE_SECTION, L"wnd_right", -1, ini_file);
	int bottom = GetPrivateProfileInt(INI_FILE_SECTION, L"wnd_bottom", -1, ini_file);

	if (right != -1)
	{
		embedWindow->r.right = right;
		WritePrivateProfileString(INI_FILE_SECTION, L"wnd_right", 0, ini_file);
	}
	else
	{
		embedWindow->r.right = embedWindow->r.left + GetPrivateProfileInt(INI_FILE_SECTION, L"SizeX", 275, ini_file);
	}

	if (bottom != -1)
	{
		embedWindow->r.bottom = bottom;
		WritePrivateProfileString(INI_FILE_SECTION, L"wnd_bottom", 0, ini_file);
	}
	else
	{
		embedWindow->r.bottom = embedWindow->r.top + GetPrivateProfileInt(INI_FILE_SECTION, L"SizeY", 232, ini_file);
	}

	CopyRect(&initial[0], &embedWindow->r);

	initial[1].top = height = GetPrivateProfileInt(INI_FILE_SECTION, L"ff_height", height, ini_file);
	initial[1].left = width = GetPrivateProfileInt(INI_FILE_SECTION, L"ff_width", width, ini_file);

	// specifying this will prevent the modern skin engine (gen_ff) from adding a menu entry
	// to the main right-click menu. this is useful if you want to add your own menu item so
	// you can show a keyboard accelerator (as we are doing) without a generic menu added
	embedWindow->flags |= EMBED_FLAGS_NOWINDOWMENU;

	// now we have set up the embedWindowState structure, we pass it to Winamp to create
	HWND frame = (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)embedWindow, IPC_GET_EMBEDIF);

	// this needs to be called so the created window will be processed in the Ctrl+Tab loop
	WASABI_API_APP->app_registerGlobalWindow(frame);

	return frame;
}

BOOL WritePrivateProfileInt(LPCWSTR lpAppName, LPCWSTR lpKeyName, int value, LPCWSTR lpFileName)
{
	WCHAR string[32] = {0};
	StringCchPrintf(string, 32, L"%d", value);
	return WritePrivateProfileString(INI_FILE_SECTION, lpKeyName, string, ini_file);
}

void DestroyEmbeddedWindow(embedWindowState* embedWindow)
{
	if (!EqualRect(&initial[0], &embedWindow->r))
	{
		WritePrivateProfileInt(INI_FILE_SECTION, L"PosX", embedWindow->r.left, ini_file);
		WritePrivateProfileInt(INI_FILE_SECTION, L"PosY", embedWindow->r.top, ini_file);
		WritePrivateProfileInt(INI_FILE_SECTION, L"SizeX", embedWindow->r.right - embedWindow->r.left, ini_file);
		WritePrivateProfileInt(INI_FILE_SECTION, L"SizeY", embedWindow->r.bottom - embedWindow->r.top, ini_file);
	}

	if (old_visible != visible)
	{
		WritePrivateProfileInt(INI_FILE_SECTION, L"wnd_open", visible, ini_file);
	}

	if (initial[1].top != height || initial[1].left != width)
	{
		WritePrivateProfileInt(INI_FILE_SECTION, L"ff_height", height, ini_file);
		WritePrivateProfileInt(INI_FILE_SECTION, L"ff_width", width, ini_file);
	}
}

void ShowEmbeddedWindow(embedWindowState* embedWindow, HWND embeddedWindow, BOOL startup)
{
	// Winamp can report if it was started minimised which allows us to control our window
	// to not properly show on startup otherwise the window will appear incorrectly when it
	// is meant to remain hidden until Winamp is restored back into view correctly
	if (startup && (SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_INITIAL_SHOW_STATE) == SW_SHOWMINIMIZED))
	{
		SetEmbeddedWindowMinimizedMode(embeddedWindow, TRUE);
	}
	else
	{
		ShowWindow(embeddedWindow, SW_SHOWNA);
	}
}

int AddItemToMenu(HMENU hmenu, UINT id, wchar_t* text, UINT pos, int fByPosition)
{
	MENUITEMINFO mii = {sizeof(MENUITEMINFO), MIIM_ID | MIIM_TYPE | MIIM_DATA,
						(text ? MFT_STRING : MFT_SEPARATOR), 0, id, 0, 0, 0, 0,
						(text ? text : 0), (text ? lstrlen(text) : 0)};

	// since this can only be 16-bit value, is better we play nicely with things
	// which will deal with some issues the skinned code in gen_ml has at times.
	if (HIWORD(id) == 0xFFFF)
	{
		mii.wID = 0xFFFF;
	}
	return InsertMenuItem(hmenu, pos, fByPosition, &mii);
}

void AddEmbeddedWindowToMenus(BOOL add, UINT menuId, LPWSTR menuString, BOOL setVisible)
{
	// this will add a menu item to the main right-click menu
	if (add)
	{
		main_menu = (HMENU)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_HMENU);

		int prior_item = GetMenuItemID(main_menu, 9);
		if (prior_item <= 0)
		{
			prior_item = GetMenuItemID(main_menu, 8);
		}
		AddItemToMenu(main_menu, menuId, menuString, prior_item, 0);
		CheckMenuItem(main_menu, menuId, MF_BYCOMMAND |
					  ((setVisible == -1 ? visible : setVisible) ? MF_CHECKED : MF_UNCHECKED));
	}
	else
	{
		DeleteMenu(main_menu, menuId, MF_BYCOMMAND);
	}

	// this will adjust the menu position (there were bugs with this api but all is fine for 5.5+)
	SendMessage(plugin.hwndParent, WM_WA_IPC, (add ? 1 : -1), IPC_ADJUST_OPTIONSMENUPOS);

	// this will add a menu item to the main window views menu
	if (add)
	{
		windows_menu = (HMENU)SendMessage(plugin.hwndParent, WM_WA_IPC, 4, IPC_GET_HMENU);

		int prior_item = GetMenuItemID(windows_menu, 3);
		if (prior_item <= 0)
		{
			prior_item = GetMenuItemID(windows_menu, 2);
		}

		AddItemToMenu(windows_menu, menuId, menuString, prior_item,0);
		CheckMenuItem(windows_menu, menuId, MF_BYCOMMAND |
					  ((setVisible == -1 ? visible : setVisible) ? MF_CHECKED : MF_UNCHECKED));
	}
	else
	{
		DeleteMenu(windows_menu,menuId,MF_BYCOMMAND);
	}

	// this will adjust the menu position (there were bugs with this api but all is fine for 5.5+)
	SendMessage(plugin.hwndParent, WM_WA_IPC, (add ? 1 : -1), IPC_ADJUST_FFWINDOWSMENUPOS);
}

void UpdateEmbeddedWindowsMenu(UINT menuId)
{
	UINT check = MF_BYCOMMAND | (visible ? MF_CHECKED : MF_UNCHECKED);
	if (main_menu)
	{
		CheckMenuItem(main_menu, menuId, check);
	}
	if (windows_menu)
	{
		CheckMenuItem(windows_menu, menuId, check);
	}
}

BOOL SetEmbeddedWindowMinimizedMode(HWND embeddedWindow, BOOL fMinimized)
{
	if (fMinimized == TRUE)
	{
		return SetProp(embeddedWindow, MINIMISED_FLAG, (HANDLE)1);
	}	
	RemoveProp(embeddedWindow, MINIMISED_FLAG);
	return TRUE;
}

BOOL EmbeddedWindowIsMinimizedMode(HWND embeddedWindow)
{
	return (GetProp(embeddedWindow, MINIMISED_FLAG) != 0);
}

LRESULT HandleEmbeddedWindowChildMessages(HWND embedWnd, UINT menuId, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// we handle both messages so we can get the action when sent via the keyboard
	// shortcut but also copes with using the menu via Winamp's taskbar system menu
	if ((message == WM_SYSCOMMAND || message == WM_COMMAND) && LOWORD(wParam) == menuId)
	{
		ShowWindow(embedWnd, (IsWindowVisible(embedWnd) ? SW_HIDE : SW_SHOW));
		visible = !visible;
		UpdateEmbeddedWindowsMenu(menuId);
		return 1;
	}
	// this is sent to the child window of the frame when the 'close' button is clicked
	else if (message == WM_CLOSE)
	{
		ShowWindow(embedWnd, SW_HIDE);
		visible = 0;
		UpdateEmbeddedWindowsMenu(menuId);
		SendMessage(plugin.hwndParent, WM_COMMAND, MAKEWPARAM(WINAMP_NEXT_WINDOW, 0), 0);
	}
	else if (message == WM_WINDOWPOSCHANGING)
	{
		/*
		 if extra_data[EMBED_STATE_EXTRA_REPARENTING] is set, we are being reparented by the freeform lib, so we should
		 just ignore this message because our visibility will not change once the freeform
		 takeover/restoration is complete
		*/
		embedWindowState *state=(embedWindowState *)GetWindowLongPtr(embedWnd,GWLP_USERDATA);
		if (state && state->reparenting && !GetParent(embedWnd))
		{
			// this will reset the position of the frame when we need it to
			// usually from going classic->modern->close->start->classic
			SetWindowPos(embedWnd, 0, 0, 0, width, height,
						 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE |
						 SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
		}
	}
	return 0;
}

LRESULT HandleEmbeddedWindowWinampWindowMessages(HWND embedWnd, UINT menuId, embedWindowState* embedWindow, BOOL preSubclass,
												 HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (preSubclass)
	{
		// these are done before the original window proceedure has been called to
		// ensure we get the correct size of the window and for checking the menu
		// item for the embedded window as applicable
		if ((message == WM_SYSCOMMAND || message == WM_COMMAND) && LOWORD(wParam) == menuId)
		{
			ShowWindow(embedWnd, (IsWindowVisible(embedWnd) ? SW_HIDE : SW_SHOW));
			visible = !visible;
			UpdateEmbeddedWindowsMenu(menuId);
			return 1;
		}

		else if (message == WM_COMMAND && LOWORD(wParam) == WINAMP_REFRESHSKIN)
		{
			if (!GetParent(embedWnd))
			{
				width = embedWindow->r.right - embedWindow->r.left;
				height = embedWindow->r.bottom - embedWindow->r.top;
			}
		}
	}
	else
	{
		// this is used to cope with Winamp being started minimised and will then
		// re-show the example window when Winamp is being restored to visibility
		if (message == WM_SIZE && wParam == SIZE_RESTORED)
		{
			if (EmbeddedWindowIsMinimizedMode(embedWnd))
			{
				ShowWindow(embedWnd, (visible ? SW_SHOWNA : SW_HIDE));
				SetEmbeddedWindowMinimizedMode(embedWnd, FALSE);
			}
		}
	}

	return 0;
}