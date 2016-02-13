#include "playbgm.h"

BOOL WINAPI HandlerRoutine(
  __in  DWORD dwCtrlType
);

HANDLE hEvent;
BOOL bBreak;

void PlayerFunc(HANDLE hFileMapping, int filesize, HANDLE hEvent, THBGM_FMT bgmfmt, int loopcount)
{
	int offst, offed, offlp; // Offset to start, end, loop start
	int size, off; // Size of the part of thbgm.dat to be mapped
	int addrfirst, sizefirst, addrloop, sizeloop; // Calculated address and size of first part and loop part in the mapped memory
	int i, count, ret;
	void* addr; // Base address of the view
	HWAVEOUT hwo;
	WAVEHDR wh;

	hwo = NULL;
	addr = NULL;
	printf("%s ", bgmfmt.filename);
	//printf("startaddr: 0x%08X size1: 0x%08X size2: 0x%08X unk: 0x%08X\n", bgmfmt.startaddr, bgmfmt.size1, bgmfmt.size2, bgmfmt.unk);
	printf("%d channel(s) %dHz %dbit ", bgmfmt.wfx.nChannels, bgmfmt.wfx.nSamplesPerSec, bgmfmt.wfx.wBitsPerSample);

	offst = bgmfmt.startaddr;
	offed = bgmfmt.startaddr + bgmfmt.size1;
	offlp = bgmfmt.startaddr + bgmfmt.size2;

	// 0x10000 align
	// MapViewOfFile only accepts aligned addresses

	off = offst & 0xFFFF0000L;
	size = (offed - off + 0x0000FFFFL) & 0xFFFF0000L;

	// If off + size is beyond the file size, MapViewOfFile will fail
	// So we set size to 0
	// MSDN: If this parameter is 0 (zero), the mapping extends from the specified offset to the end of the file mapping

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

	ret = waveOutOpen(&hwo, WAVE_MAPPER, &bgmfmt.wfx, (DWORD_PTR)hEvent, 0, CALLBACK_EVENT);

	// Note: waveOutOpen will set event to signaled
	//ResetEvent(hEvent);

	if (ret)
	{
		printf("waveOutOpen failed with error %d\n", ret);
		goto _onexit;
	}

	memset(&wh, 0, sizeof(wh));
	wh.lpData = (LPSTR)addrfirst;
	wh.dwBufferLength = sizefirst;
	wh.dwFlags = 0;
	wh.dwLoops = 1;

	waveOutPrepareHeader(hwo, &wh, sizeof(wh));
	waveOutWrite(hwo, &wh, sizeof(wh));
	waveOutSetVolume(hwo, 0xffffffff);

	ResetEvent(hEvent);

	// We need to pause for a while so that the waveOut function can work normally
	// And we wait for the event in case the user uses CTRL+C during the time
	WaitForSingleObject(hEvent, 1000);

	// We can call waveOutWrite on the same HWAVEOUT for more than one time, thus there will not be any break in the loop

	memset(&wh, 0, sizeof(wh));
	wh.lpData = (LPSTR)addrloop;
	wh.dwBufferLength = sizeloop;
	wh.dwFlags = 0;
	wh.dwLoops = 1;

	count = loopcount;

	while (!bBreak && count != 0)
	{
		printf("=");
		waveOutPrepareHeader(hwo, &wh, sizeof(wh));
		waveOutWrite(hwo, &wh, sizeof(wh));

		WaitForSingleObject(hEvent, INFINITE);
		count -= 1;
	}

	if (!bBreak) {
		printf(">");
		waveOutPrepareHeader(hwo, &wh, sizeof(wh));
		waveOutWrite(hwo, &wh, sizeof(wh));
		for (i = 0xffff; i >= 0x2000; i-=80) {
			waveOutSetVolume(hwo, i << 16 | i);
			if (bBreak) break;
			Sleep(10);
		}
	}

	printf("\n");

_onexit:
	if (hwo) {
		waveOutReset(hwo);
		waveOutClose(hwo);
	}
	if (addr) UnmapViewOfFile(addr);
}

int _tmain(int argc, _TCHAR* argv[])
{
	HANDLE hFile, hFileMapping;
	
	//WAVEFORMATEX wfx;
	int filesize; // Size of the thbgm.dat file, and we assume it is not larger then 4GB
	int i, ret, loopcount;
	FILE* fmtfile; // thbgm.fmt file
	THBGM_FMT bgmfmt[64];
	bool playall;

	// Initialize variables
	// Otherwise there will be errors in _onexit
	hFile = INVALID_HANDLE_VALUE;
	hFileMapping = NULL;
	hEvent = NULL;
	fmtfile = NULL;

	printf("Usage: playbgm <thbgm.dat> <thbgm.fmt> <index> [<loop count>]\n");
	printf("\n");
	printf("<thbgm.dat>	Specify thbgm.dat file.\n");
	printf("<thbgm.fmt>	Specify thbgm.fmt file.\n");
	printf("<index>		Specify which track to play. * means playing all tracks.\n");
	printf("<loop count>	Optional. Specify loop times for each track. For playing\n");
	printf("		single track, the default value is infinite; for playing\n");
	printf("		all tracks, the default value is 1. Must be greater than\n");
	printf("		zero.\n");
	printf("\n");

	switch (argc) {
	case 4:
		loopcount = 0;
		break;
	case 5:
		swscanf_s(argv[4], L"%d", &loopcount);
		if (loopcount <= 0) {
			printf("Loop count must be greater than zero");
			goto _onexit;
		}
		break;
	default:
		goto _onexit;
	}

	ret = _wfopen_s(&fmtfile, argv[2], L"rb");
	if (ret)
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

	if (!lstrcmp(argv[3], L"*")) {
		playall = true;
		if (loopcount == 0) loopcount = 1;
	}
	else {
		playall = false;
		if (loopcount == 0) loopcount = -1;
		swscanf_s(argv[3], L"%d", &i);
		if (i < 0 || i >= 64 || !strcmp(bgmfmt[i].filename, "")) {
			printf("number is out of range");
			goto _onexit;
		}
	}

	hFile = CreateFile(argv[1], FILE_ALL_ACCESS, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf("CreateFile failed, last error %x\n", GetLastError());
		goto _onexit;
	}

	hEvent = CreateEvent(NULL, 0, 0, NULL);
	bBreak = FALSE;

	hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

	if (!hFileMapping)
	{
		printf("CreateFileMapping failed, last error %x\n", GetLastError());
		goto _onexit;
	}

	filesize = GetFileSize(hFile, NULL);

	SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	printf("Use CTRL+C to stop\n");
	if (playall){
		i = 0;
		while (!bBreak) {
			if (!strcmp(bgmfmt[i].filename, "")) i = 0;
			PlayerFunc(hFileMapping, filesize, hEvent, bgmfmt[i], loopcount);
			i++;
		}
	}
	else {
		PlayerFunc(hFileMapping, filesize, hEvent, bgmfmt[i], loopcount);
	}

_onexit:
	printf("\n");
	if (hEvent) CloseHandle(hEvent);
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
