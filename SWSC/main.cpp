
#pragma comment(lib,"Ws2_32.lib")

/*******************************************************************************
	[Includes]
*******************************************************************************/
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <conio.h>

/*******************************************************************************
	[Structures]
*******************************************************************************/
struct sClient
{
	unsigned char iID;
	SOCKET hSock;
	struct sClient *pmNext;
};

/*******************************************************************************
	[Globals]
*******************************************************************************/
static HANDLE ghHeap;
static HANDLE ghCSSocket;
static HANDLE ghENDone;
static unsigned char giConnection;
static unsigned char giID;
struct sClient gmClients;

/*******************************************************************************
	[Routines]
*******************************************************************************/
struct sClient *rServiceAdd(void)
{
	struct sClient *pmClient;
	WaitForSingleObject(ghCSSocket, INFINITE);
	giConnection++;
	giID++;
	pmClient = (struct sClient *)HeapAlloc(ghHeap, 0, sizeof(struct sClient));
	pmClient->hSock = gmClients.hSock;
	pmClient->pmNext = gmClients.pmNext;
	pmClient->iID = giID;
	gmClients.pmNext = pmClient;
	ReleaseSemaphore(ghCSSocket, 1, NULL);
	return pmClient;
}
void rServiceRemove(struct sClient *pmClient)
{
	struct sClient **ppmClient;
	WaitForSingleObject(ghCSSocket, INFINITE);
	ppmClient = &gmClients.pmNext;
	while (*ppmClient != pmClient)
	{
		ppmClient = &(*ppmClient)->pmNext;
	}
	*ppmClient = pmClient->pmNext;
	HeapFree(ghHeap, 0, pmClient);
	giConnection--;
	ReleaseSemaphore(ghCSSocket, 1, NULL);
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
			//printf("# [%d], Request: %s...",pmClient->iID,cURI);
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

			//	Open Requested File
			hFile = CreateFileW(cPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
			//		Invalid
			if (hFile == INVALID_HANDLE_VALUE)
			{
				//
				strcat_s(cStatus, 128, "404 Not Found");
				strcat_s(cStatus, 128, "\r\n");
				send(pmClient->hSock, cStatus, strlen(cStatus), 0);

				//
				wsprintfA(cDigits, "%d", 0);
				strcat_s(cLength, 128, cDigits);
				strcat_s(cLength, 128, "\r\n");
				send(pmClient->hSock, cLength, strlen(cLength), 0);

				//
				send(pmClient->hSock, "\r\n", 2, 0);
				printf("# [%d] 404\n", pmClient->iID);
				continue;
			}
			//
			strcat_s(cStatus, 128, "200 OK");
			strcat_s(cStatus, 128, "\r\n");
			send(pmClient->hSock, cStatus, strlen(cStatus), 0);
			//
			wsprintfA(cDigits, "%d", GetFileSize(hFile, NULL));
			strcat_s(cLength, 128, cDigits);
			strcat_s(cLength, 128, "\r\n");
			send(pmClient->hSock, cLength, strlen(cLength), 0);
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
			printf("# [%d] 200\n", pmClient->iID);
		}
	}
	goto Close;
Error:
	{
		printf("! Unsupport Mode\n");
	}
Close:
	{
		if (pmClient->hSock != INVALID_SOCKET) { closesocket(pmClient->hSock); }
		printf("# [%d], Close\n", pmClient->iID);
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
			printf("Service Done\n");
			break;
		}
		//	Create a new socket node.
		pmClient = rServiceAdd();
		printf("# [%d], Open\n", pmClient->iID);
		CreateThread(NULL, 0, rLoopService, (LPVOID)pmClient, 0, NULL);

	} while (1);
	return 0;
}

int main(void)
{
	int iResult;
	struct addrinfo *pmAddr;
	SOCKET hSockListen;
	WCHAR cDirectory[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, cDirectory);
	MessageBox(NULL, cDirectory, L"Working Directory", MB_OK);


	//WSA Startup
	{
		WSADATA mWSA;
		iResult = WSAStartup(MAKEWORD(2, 2), &mWSA);
		if (iResult != 0)
		{
			printf("! WSA Startup Fail (%d)\n", iResult);
			return 1;
		}
	}

	//Get Address Info
	{
		struct addrinfo mAddr;
		ZeroMemory(&mAddr, sizeof(struct addrinfo));
		mAddr.ai_family = AF_INET;
		mAddr.ai_socktype = SOCK_STREAM;
		mAddr.ai_protocol = IPPROTO_TCP;
		mAddr.ai_flags = AI_PASSIVE;
		iResult = getaddrinfo(NULL, "7788", &mAddr, &pmAddr);
		if (iResult != 0)
		{
			printf("! Get Address Info Fail (%d)\n", iResult);
			WSACleanup();
			return 1;
		}
	}

	//Create Listen Socket
	hSockListen = socket(pmAddr->ai_family, pmAddr->ai_socktype, pmAddr->ai_protocol);
	if (hSockListen == INVALID_SOCKET)
	{
		printf("! Listen Socket Creation Fail (%d)\n", WSAGetLastError());
		freeaddrinfo(pmAddr);
		WSACleanup();
		return 1;
	}

	//Bind Socket
	iResult = bind(hSockListen, pmAddr->ai_addr, (int)pmAddr->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		printf("! Bind Socket Fail (%d)\n", WSAGetLastError());
		freeaddrinfo(pmAddr);
		closesocket(hSockListen);
		WSACleanup();
		return 1;
	}
	freeaddrinfo(pmAddr);

	//Listen
	iResult = listen(hSockListen, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		printf("! Listen Socket Fail (%d)\n", WSAGetLastError());
		closesocket(hSockListen);
		WSACleanup();
		return 1;
	}


	{
		ghHeap = HeapCreate(0, 0, 0);
		ghCSSocket = CreateSemaphoreA(NULL, 1, 1, "Critical Section Sempaphore");
		giConnection = 0;
		giID = 0;
		gmClients.pmNext = NULL;
		printf("# Service Start\n");

		CreateThread(NULL, 0, rLoopListen, (LPVOID)hSockListen, 0, NULL);
		printf("# Press Z to Finish Service\n");
		char cKey;
		do
		{
			printf(">");
			cKey = _getch();
			printf("%c\n", cKey);
		} while (cKey != 'Z');
		closesocket(hSockListen);

		printf("# Press Any Key to Finish this Application\n");
		_getch();

		CloseHandle(ghCSSocket);
		HeapDestroy(ghHeap);
	}


	return 0;
}