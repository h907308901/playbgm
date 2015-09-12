#include <tchar.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")

// http://tieba.baidu.com/p/2527936559

struct THBGM_FMT
{
	CHAR filename[15];
	int startaddr;
	int unk;
	int size2;
	int size1;
	WAVEFORMATEX wfx;
	WORD reserved;
};