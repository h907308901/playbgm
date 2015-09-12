#include "playbgm.h"

BOOL WINAPI HandlerRoutine(
  __in  DWORD dwCtrlType
);

HANDLE hEvent;
BOOL bBreak;

int _tmain(int argc, _TCHAR* argv[])
{
	HANDLE hFile, hFileMapping;
	HWAVEOUT hwo;
	WAVEHDR wh;
	//WAVEFORMATEX wfx;
	int filesize; // Size of the thbgm.dat file, and we assume it is not larger then 4GB
	int offst, offed, offlp; // Offset to start, end, loop start
	int size, off, ret; // Size of the part of thbgm.dat to be mapped
	int addrfirst, sizefirst, addrloop, sizeloop; // Calculated address and size of first part and loop part in the mapped memory
	int i;
	void* addr; // Base address of the view
	FILE* fmtfile; // thbgm.fmt file
	THBGM_FMT bgmfmt[64];

	// Initislize variables
	// Otherwise there will be errors in _onexit
	hFile = INVALID_HANDLE_VALUE;
	hFileMapping = NULL;
	hwo = NULL;
	hEvent = NULL;
	fmtfile = NULL;
	addr = NULL;

	printf("Usage: playbgm <thbgm.dat> <thbgm.fmt> <internal no>\n\n");

	if (argc != 4) goto _onexit;

	fmtfile = _wfopen(argv[2], L"rb");
	
	if (!fmtfile)
	{
		printf("open fmt file failed");
		goto _onexit;
	}

	ZeroMemory(&bgmfmt[0], sizeof(bgmfmt));

	for (i = 0; i < 64; i++)
	{
		// Note: the last item only contains a 16-byte string and a 1-byte terminator, instead of a complete THBGM_FMT
		fread(&bgmfmt[i], sizeof(THBGM_FMT), 1, fmtfile);
		if (!strcmp(bgmfmt[i].filename, ""))
			break;
		//printf("%s\n", bgmfmt[i].filename);
	}

	swscanf(argv[3], L"%d", &i);
	if (i < 0 || i >=64 || !strcmp(bgmfmt[i].filename, "")) {
		printf("number is out of range");
		goto _onexit;
	}

	printf("%s\n", bgmfmt[i].filename);
	//printf("startaddr: 0x%08X size1: 0x%08X size2: 0x%08X unk: 0x%08X\n", bgmfmt[i].startaddr, bgmfmt[i].size1, bgmfmt[i].size2, bgmfmt[i].unk);
	printf("%d channel(s) %dHz %dbit\n", bgmfmt[i].wfx.nChannels, bgmfmt[i].wfx.nSamplesPerSec, bgmfmt[i].wfx.wBitsPerSample);
	printf("\n");

	offst = bgmfmt[i].startaddr;
	offed = bgmfmt[i].startaddr + bgmfmt[i].size1;
	offlp = bgmfmt[i].startaddr + bgmfmt[i].size2;

	hFile = CreateFile(argv[1], FILE_ALL_ACCESS, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf("CreateFile failed, last error %x\n", GetLastError());
		goto _onexit;
	}

	hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

	if (!hFileMapping)
	{
		printf("CreateFileMapping failed, last error %x\n", GetLastError());
		goto _onexit;
	}

	// 0x10000 align
	// MapViewOfFile only accepts aligned addresses

	off = offst & 0xFFFF0000L;
	size = (offed - off + 0x0000FFFFL) & 0xFFFF0000L;

	// If off + size is beyond the file size, MapViewOfFile will fail
	// So we set size to 0
	// MSDN: If this parameter is 0 (zero), the mapping extends from the specified offset to the end of the file mapping

	filesize = GetFileSize(hFile, NULL);
	if (off + size > filesize)
		size = 0;

	//printf("START: 0x%08X\n", offst);
	//printf("END:   0x%08X\n", offed);
	//printf("LOOP:   0x%08X\n", offlp);
	//printf("ALIGNED OFFSET: 0x%08X\n", off);
	//printf("ALIGNED SIZE:   0x%08X\n", size);

	addr = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, off, size);

	if (!addr)
	{
		printf("MapViewOfFile failed, last error %x\n", GetLastError());
		goto _onexit;
	}

	//printf("BASE ADDRESS: 0x%08X\n", addr);

	// This is the default thbgm format, but we can get it from thbgm.fmt

	/*wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = 44100L;
	wfx.nAvgBytesPerSec = 176400L;
	wfx.nBlockAlign = 4;
	wfx.wBitsPerSample = 16;
	wfx.cbSize = 0;*/

	hEvent = CreateEvent(NULL, 0, 0, NULL);
	bBreak = FALSE;

	SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	ret = waveOutOpen(&hwo, WAVE_MAPPER, &bgmfmt[i].wfx, (DWORD_PTR)hEvent, 0, CALLBACK_EVENT);

	// Note: waveOutOpen will set event to signaled
	//ResetEvent(hEvent);

	if (ret)
	{
		printf("waveOutOpen failed with error %d\n", ret);
		goto _onexit;
	}

	addrfirst = (DWORD)addr + offst - off;
	sizefirst = offed - offst;;
	addrloop = (DWORD)addr + offlp - off;
	sizeloop = offed - offlp;

	//printf("\n");
	//printf("addrfirst: 0x%08X\n", addrfirst);
	//printf("sizefirst: 0x%08X\n",sizefirst);
	//printf("addrloop: 0x%08X\n", addrloop);
	//printf("sizeloop: 0x%08X\n",sizeloop);
	//printf("\n");

	printf("Use CTRL+C to stop\n");

	memset(&wh, 0, sizeof(wh));
	wh.lpData = (LPSTR)addrfirst;
	wh.dwBufferLength = sizefirst;
	wh.dwFlags = 0;
	wh.dwLoops = 1;

	waveOutPrepareHeader(hwo, &wh, sizeof(wh));
	waveOutWrite(hwo, &wh, sizeof(wh));

	ResetEvent(hEvent);

	// We need to pause for a while so that the wavOut function can work normally
	// And we wait for the event in case the user uses CTRL+C during the time
	WaitForSingleObject(hEvent, 1000);

	// We can call waveOutWrite on the same HWAVEOUT for more than one time, thus there will not be any break in the loop

	memset(&wh, 0, sizeof(wh));
	wh.lpData = (LPSTR)addrloop;
	wh.dwBufferLength = sizeloop;
	wh.dwFlags = 0;
	wh.dwLoops = 1;

	while (!bBreak)
	{
		printf(".");
		waveOutPrepareHeader(hwo, &wh, sizeof(wh));
		waveOutWrite(hwo, &wh, sizeof(wh));

		WaitForSingleObject(hEvent, INFINITE);
	}

	printf("\n");

_onexit:
	if (hwo) waveOutClose(hwo);
	if (hEvent) CloseHandle(hEvent);
	if (addr) UnmapViewOfFile(addr);
	if (hFileMapping) CloseHandle(hFileMapping);
	if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
	if (fmtfile) fclose(fmtfile);
	return 0;
}

// We use this to accept CTRL+C message, which stops the player
BOOL WINAPI HandlerRoutine(
  __in  DWORD dwCtrlType
)
{
	printf("^C\n");
	bBreak = TRUE;
	SetEvent(hEvent);

	return TRUE;
}