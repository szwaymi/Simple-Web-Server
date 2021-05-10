
/******************************************************************************
	[Configurations]
******************************************************************************/
#pragma comment (lib, "Ws2_32.lib")

/******************************************************************************
	[Includes]
******************************************************************************/
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <CommCtrl.h>
#include "resource.h"

/******************************************************************************
	[Constants]
******************************************************************************/
#define V_STA_STOPPED	0x00
#define V_STA_RUNNING	0x01

/******************************************************************************
	[Structures]
******************************************************************************/
struct sClient
{
	unsigned char iID;
	SOCKET hSock;
	struct sClient *pmNext;
};

/******************************************************************************
	[Globals]
******************************************************************************/
static HINSTANCE ghInstance;
static NOTIFYICONDATA gmND;
static unsigned char giStatus;
static struct sClient gmClients;
static unsigned char giID;
static HANDLE ghSemaphore[2];
static HANDLE ghHeap;
static HWND ghWndLog;
static HWND ghWndClients;

/******************************************************************************
	[Routines]
******************************************************************************/
void rLogAdd(unsigned char iID, const WCHAR *pcMessage)
{
	WaitForSingleObject(ghSemaphore[1], INFINITE);
	LVITEM mItem;
	{
		unsigned long iNumber;
		iNumber = ListView_GetItemCount(ghWndLog);
		if (iNumber > 100)
		{
			ListView_DeleteItem(ghWndLog, 100);
		}

	}
	//Date Time
	{
		WCHAR cDT[24];
		WCHAR cTemp[12];
		GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, L"yyyy-MM-dd", cTemp, 12);
		lstrcpy(cDT, cTemp);
		lstrcat(cDT, L" ");
		GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, L"HH:mm:ss", cTemp, 12);
		lstrcat(cDT, cTemp);
		mItem.pszText = cDT;
		mItem.mask = LVIF_TEXT;
		mItem.iItem = 0;
		mItem.iSubItem = 0;
		ListView_InsertItem(ghWndLog, &mItem);
	}
	//Socket ID
	{
		WCHAR cID[6];
		wsprintf(cID, L"%d", iID);
		mItem.pszText = cID;
		mItem.iSubItem = 1;
		ListView_SetItem(ghWndLog, &mItem);
	}
	{
		mItem.pszText = (WCHAR *)pcMessage;
		mItem.iSubItem = 2;
		ListView_SetItem(ghWndLog, &mItem);
	}
	ReleaseSemaphore(ghSemaphore[1], 1, NULL);
}
struct sClient *rServiceAdd(void)
{
	struct sClient *pmClient;
	WaitForSingleObject(ghSemaphore, INFINITE);
	giID++;
	pmClient = (struct sClient *)HeapAlloc(ghHeap, 0, sizeof(struct sClient));
	if (pmClient)
	{
		pmClient->hSock = gmClients.hSock;
		pmClient->pmNext = gmClients.pmNext;
		pmClient->iID = giID;
		gmClients.pmNext = pmClient;
	}
	ReleaseSemaphore(ghSemaphore, 1, NULL);
	return pmClient;
}
void rServiceRemove(struct sClient *pmClient)
{
	struct sClient **ppmClient;
	WaitForSingleObject(ghSemaphore, INFINITE);
	ppmClient = &gmClients.pmNext;
	while (*ppmClient != pmClient)
	{
		ppmClient = &(*ppmClient)->pmNext;
	}
	*ppmClient = pmClient->pmNext;
	HeapFree(ghHeap, 0, pmClient);
	ReleaseSemaphore(ghSemaphore, 1, NULL);
}
DWORD WINAPI rLoopService(LPVOID pParam)
{
	struct sClient *pmClient;
	int iResult;
	struct
	{
		unsigned long iSize;
		unsigned char iData[512];
	}mURI;
	pmClient = (struct sClient *)pParam;

	while (1)
	{
		//[Request]
		//	Request Message Parsing
		{
			//		Start Line (Request Line)
			{
				//			Method (GET)
				{
					char cMethod[4];
					iResult = recv(pmClient->hSock, cMethod, 4, MSG_WAITALL);
					if (iResult == 0) { goto Close; }
					cMethod[3] = 0;
					if (strcmp(cMethod, "GET") || iResult != 4) { goto Error; }
				}
				//			Request URI
				{
					unsigned char *pcData;
					mURI.iSize = 0;
					pcData = mURI.iData;
					while (1)
					{
						iResult = recv(pmClient->hSock, (char *)pcData, 1, MSG_WAITALL);
						if (iResult == 0) { goto Close; }
						if (*pcData == ' ') { break; }
						else if (*pcData == '%')
						{
							unsigned char iCon;
							char cMsg;
							*pcData = 0;
							for (iCon = 0; iCon < 2; iCon++)
							{
								iResult = recv(pmClient->hSock, &cMsg, 1, MSG_WAITALL);
								if (cMsg <= '9' && cMsg >= '0') { *pcData += (cMsg - '0'); }
								else if (cMsg <= 'F' && cMsg >= 'A') { *pcData += (cMsg - 'A' + 10); }
								else if (cMsg <= 'f' && cMsg >= 'a') { *pcData += (cMsg - 'a' + 10); }
								*pcData <<= ((1 - iCon) * 4);
							}
						}
						pcData++;
						mURI.iSize++;
					}
				}
				//			HTTP-Version
				{
					char *pcVersion;
					char cVersion[16];
					pcVersion = cVersion;
					while (1)
					{
						iResult = recv(pmClient->hSock, pcVersion, 1, MSG_WAITALL);
						if (iResult == 0) { goto Close; }
						if (*pcVersion == 10)
						{
							pcVersion--;
							if (*pcVersion != 13) { goto Error; }
							*pcVersion = 0;
							break;
						}
						pcVersion++;
					}
				}
			}
			//		Message Headers
			{
				char cMsg;
				unsigned long iSize;
				do
				{
					iSize = 0;
					while (1)
					{
						iResult = recv(pmClient->hSock, &cMsg, 1, 0);
						if (iResult == 0) { goto Close; }
						if (cMsg == 10) { break; }
						iSize++;
					}
				} while (iSize != 1);
			}
			//		Message Body
			{
				//Assume No Message Body
			}
		}

		//[Response]
		{
			HANDLE hFile;
			WCHAR cPath[512];
			char cStatus[128] = { "HTTP/1.1 " };
			char cLength[128] = { "Content-Length:" };
			char cDigits[12];
			//	Create Corresponsed Local Path
			{
				unsigned long iCon;
				WCHAR *pcPath;

				lstrcpy(cPath, L".");
				pcPath = cPath;
				while (*pcPath != NULL) { pcPath++; }
				iCon = 0;
				do
				{
					if ((mURI.iData[iCon] & 0x80) == 0x00) {
						*pcPath = (WCHAR)mURI.iData[iCon];
						iCon++;
					}
					else if ((mURI.iData[iCon] & 0xE0) == 0xC0) {
						*pcPath = (((WCHAR)(mURI.iData[iCon] & 0x1F)) << 6) | ((WCHAR)(mURI.iData[iCon + 1] & 0x3F));
						iCon += 2;
					}
					else if ((mURI.iData[iCon] & 0xF0) == 0xE0) {
						*pcPath = (((WCHAR)(mURI.iData[iCon] & 0x1F)) << 12) | (((WCHAR)(mURI.iData[iCon + 1] & 0x3F)) << 6) | ((WCHAR)(mURI.iData[iCon + 2] & 0x3F));
						iCon += 3;
					}
					pcPath++;
				} while (iCon <= mURI.iSize);
				*pcPath = 0;
			}
			rLogAdd(pmClient->iID, cPath);
			//	Open Requested File
			hFile = CreateFileW(cPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
			//		Invalid
			if (hFile == INVALID_HANDLE_VALUE)
			{
				//
				strcat_s(cStatus, 128, "404 Not Found");
				strcat_s(cStatus, 128, "\r\n");
				send(pmClient->hSock, cStatus, (int)strlen(cStatus), 0);

				//
				wsprintfA(cDigits, "%d", 0);
				strcat_s(cLength, 128, cDigits);
				strcat_s(cLength, 128, "\r\n");
				send(pmClient->hSock, cLength, (int)strlen(cLength), 0);

				//
				send(pmClient->hSock, "\r\n", 2, 0);
				rLogAdd(pmClient->iID, L"404");
				continue;
			}
			//
			strcat_s(cStatus, 128, "200 OK");
			strcat_s(cStatus, 128, "\r\n");
			send(pmClient->hSock, cStatus, (int)strlen(cStatus), 0);
			//
			wsprintfA(cDigits, "%d", GetFileSize(hFile, NULL));
			strcat_s(cLength, 128, cDigits);
			strcat_s(cLength, 128, "\r\n");
			send(pmClient->hSock, cLength, (int)strlen(cLength), 0);
			//
			send(pmClient->hSock, "\r\n", 2, 0);
			unsigned long iDone;
			do
			{
				char cBuffer[1024];
				ReadFile(hFile, cBuffer, 1024, &iDone, 0);
				if (iDone > 0)
					send(pmClient->hSock, cBuffer, iDone, 0);
			} while (iDone);
			CloseHandle(hFile);
			rLogAdd(pmClient->iID, L"200");
		}
	}
	goto Close;
Error:
	{
		rLogAdd(pmClient->iID, L"Unsupport");
	}
Close:
	{
		if (pmClient->hSock != INVALID_SOCKET) { closesocket(pmClient->hSock); }
		//printf("# [%d], Close\n", pmClient->iID);
		rServiceRemove(pmClient);

	}
	return 0;
}

DWORD WINAPI rLoopListen(LPVOID pParam)
{
	struct sClient *pmClient;
	SOCKET hListen;

	do
	{
		hListen = (SOCKET)pParam;
		gmClients.hSock = accept(hListen, NULL, NULL);
		if (gmClients.hSock == INVALID_SOCKET)
		{
			//printf("Service Done\n");
			break;
		}
		//	Create a new socket node.
		pmClient = rServiceAdd();
		//printf("# [%d], Open\n", pmClient->iID);
		CreateThread(NULL, 0, rLoopService, (LPVOID)pmClient, 0, NULL);

	} while (1);
	return 0;
}

LRESULT CALLBACK rProc(HWND hWnd, UINT iMsg, WPARAM iPW, LPARAM iPL)
{
	static HWND hWndStatus;
	switch (iMsg)
	{
	case WM_SHELL_NOTIFY: {
		if (iPL == WM_LBUTTONDOWN)
		{
			ShowWindow(hWnd, SW_SHOWNORMAL);
		}
		return 0; }
	case WM_CREATE: {
		RECT mClient;
		GetClientRect(hWnd, &mClient);
		//	Add Notification Icon
		ZeroMemory(&gmND, sizeof(NOTIFYICONDATA));
		gmND.cbSize = sizeof(NOTIFYICONDATA);
		gmND.hWnd = hWnd;
		gmND.uID = 0;
		gmND.uFlags = NIF_ICON | NIF_MESSAGE;
		gmND.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		gmND.uCallbackMessage = WM_SHELL_NOTIFY;
		Shell_NotifyIcon(NIM_ADD, &gmND);
		//	Add
		ghWndClients = CreateWindow(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_STANDARD | LBS_HASSTRINGS | LBS_SORT,
			0, 0, 100, mClient.bottom - mClient.top,
			hWnd, (HMENU)IDC_LB_CLIENTS, ghInstance, NULL);
		//	Add List View
		{
			//	Create List View
			ghWndLog = CreateWindow(WC_LISTVIEW, L"", WS_CHILD | LVS_REPORT | LVS_EDITLABELS | WS_VISIBLE | WS_BORDER,
				101, 0, mClient.right - mClient.left - 2, mClient.bottom - mClient.top - 2,
				hWnd, (HMENU)IDC_LV_LOG, ghInstance, NULL);
			{
				LVCOLUMN mColumn;
				//	Edit Header
				//		Fix
				mColumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
				mColumn.fmt = LVCFMT_LEFT;
				//		Var
				//			Time
				mColumn.pszText = (WCHAR *)L"Time";
				mColumn.iSubItem = 0;
				mColumn.cx = 200;
				ListView_InsertColumn(ghWndLog, 0, &mColumn);
				//			Socket
				mColumn.pszText = (WCHAR *)L"Socket";
				mColumn.iSubItem = 1;
				mColumn.cx = 100;
				ListView_InsertColumn(ghWndLog, 1, &mColumn);
				//			Socket
				mColumn.pszText = (WCHAR *)L"Message";
				mColumn.iSubItem = 2;
				mColumn.cx = mClient.right - mClient.left - 322;
				ListView_InsertColumn(ghWndLog, 2, &mColumn);
			}
		}
		/*
		//	Add a Button
		CreateWindow(L"BUTTON", L"Start Service",WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			10, 10, 100, 20, hWnd, (HMENU)IDC_BUTTON_SERVICE, ghInstance, NULL);
		//	Add a Statu?
		hWndStatus = CreateWindow(L"EDIT", L"Service Stopped", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER ,
			120, 10, 200, 20, hWnd, (HMENU)IDC_EDIT_SERVICE, ghInstance, NULL);
		//	Add another button
		CreateWindow(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			10, 40, 100, 20, hWnd, (HMENU)IDC_BUTTON_DIR, ghInstance, NULL);
		CreateWindow(L"EDIT", L"Service Stopped", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER,
			120, 40, 200, 20, hWnd, (HMENU)IDC_EDIT_DIR, ghInstance, NULL);
		*/
		return 0; }
	case WM_COMMAND: {
		if (iPL == 0)
		{//	Menu / Acc
			switch (HIWORD(iPW))
			{
			case 0:	//Menu
				switch (LOWORD(iPW))
				{
				case IDM_MAIN_FILE_EXIT: {	//Main Menu/File/Exit
					Shell_NotifyIcon(NIM_DELETE, &gmND);
					PostQuitMessage(0); }
										 break;
				}
				break;
			case 1:	//ACC
				break;
			}
		}
		else
		{
			/*
			switch (LOWORD(iPW))
			{
			case IDC_BUTTON_DIR: {

				return 0; }
			case IDC_BUTTON_SERVICE: {
				switch (giStatus)
				{
				case V_STA_RUNNING:
					SendMessage(hWndStatus, WM_SETTEXT, NULL, (LPARAM)L"Service is Stopping");
					giStatus = V_STA_STOPPED;
					break;
				case V_STA_STOPPED:
					SendMessage(hWndStatus, WM_SETTEXT, NULL, (LPARAM)L"Service is Starting");
					giStatus = V_STA_RUNNING;
					break;
				}
				return 0; }
			}
			*/
		}
		return 0; }
	case WM_CLOSE: {
		ShowWindow(hWnd, SW_HIDE);
		return 0; }
	case WM_DESTROY: {
		PostQuitMessage(0);
		return 0; }
	case WM_PAINT: {
		PAINTSTRUCT mPS;
		HDC hDC = BeginPaint(hWnd, &mPS);
		EndPaint(hWnd, &mPS);
		return 0; }
	}
	return DefWindowProc(hWnd, iMsg, iPW, iPL);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int iCmd)
{
	const wchar_t cClass[] = L"WC: Simple Server GUI";
	struct addrinfo *pmAddr;
	SOCKET hListen;

	ghInstance = hInstance;

	//WSA Startup
	{
		int iResult;
		WSADATA mWSA;
		iResult = WSAStartup(MAKEWORD(2, 2), &mWSA);
		if (iResult != 0) { MessageBox(NULL, L"Error", L"WSAStartup Fail", MB_OK | MB_ICONERROR); }
	}

	//Get Address Info
	{
		int iResult;
		struct addrinfo mAddr;
		ZeroMemory(&mAddr, sizeof(struct addrinfo));
		mAddr.ai_family = AF_INET;
		mAddr.ai_socktype = SOCK_STREAM;
		mAddr.ai_protocol = IPPROTO_TCP;
		mAddr.ai_flags = AI_PASSIVE;
		iResult = getaddrinfo(NULL, "7788", &mAddr, &pmAddr);
		if (iResult != 0)
		{
			MessageBox(NULL, L"Error", L"Get Addresss Info Fail", MB_OK | MB_ICONERROR);
			WSACleanup();
			return 1;
		}
	}

	//Create Listen Socket
	{
		hListen = socket(pmAddr->ai_family, pmAddr->ai_socktype, pmAddr->ai_protocol);
		if (hListen == INVALID_SOCKET)
		{
			MessageBox(NULL, L"Error", L"Listen Socket Creation Fail", MB_OK | MB_ICONERROR);
			freeaddrinfo(pmAddr);
			WSACleanup();
			return 1;
		}
	}

	//Bind Socket
	{
		int iResult;
		iResult = bind(hListen, pmAddr->ai_addr, (int)pmAddr->ai_addrlen);
		if (iResult == SOCKET_ERROR)
		{
			MessageBox(NULL, L"Error", L"Bind Socket Fail", MB_OK | MB_ICONERROR);
			freeaddrinfo(pmAddr);
			closesocket(hListen);
			WSACleanup();
			return 1;
		}
		freeaddrinfo(pmAddr);
	}

	//Listen
	{
		int iResult;
		iResult = listen(hListen, SOMAXCONN);
		if (iResult == SOCKET_ERROR)
		{
			MessageBox(NULL, L"Error", L"Listen Socket Fail", MB_OK | MB_ICONERROR);
			closesocket(hListen);
			WSACleanup();
			return 1;
		}
	}

	giID = 0;
	ghHeap = HeapCreate(0, 0, 0);
	ghSemaphore[0] = CreateSemaphore(NULL, 1, 1, L"Critical Section Sempaphore: Client");
	ghSemaphore[1] = CreateSemaphore(NULL, 1, 1, L"Critical Section Sempaphore: Log");
	CreateThread(NULL, 0, rLoopListen, (LPVOID)hListen, 0, NULL);

	//Register Window Class
	{
		WNDCLASS mWC = { };
		mWC.lpfnWndProc = rProc;
		mWC.hInstance = hInstance;
		mWC.lpszClassName = cClass;
		mWC.hCursor = LoadCursor(NULL, IDC_ARROW);
		mWC.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		mWC.lpszMenuName = MAKEINTRESOURCE(IDR_MENU_MAIN);
		RegisterClass(&mWC);
	}

	//Create Window
	{
		HWND hWnd = CreateWindowEx(
			0, cClass, L"Simple Server GUI", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, hInstance, NULL
		);
		if (hWnd == NULL)
		{
			MessageBox(NULL, L"Error", L"The Window can't be Created", MB_OK | MB_ICONERROR);
			return 1;
		}
		ShowWindow(hWnd, iCmd);
	}

	//Message Loop
	{
		MSG mMsg = { };
		while (GetMessage(&mMsg, NULL, 0, 0))
		{
			TranslateMessage(&mMsg);
			DispatchMessage(&mMsg);
		}
	}
	closesocket(hListen);
	CloseHandle(ghSemaphore[0]);
	CloseHandle(ghSemaphore[1]);
	if (ghHeap) { HeapDestroy(ghHeap); }

	return 0;
}
