/*--------------------------------------------------------------------
 *    The MB-system:  mb_exitflush_win32.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Flush every stdio stream when a Windows program exits.
 *
 * mbaux links GMT, and GMT loads GDAL. With gdal_w64.dll in a process, the
 * C runtime's own exit-time flush of stdio buffers no longer happens: a
 * program that prints to a pipe or a file (mbgrid -H | more, mbsegypsd,
 * mbmosaic, ...) and then calls exit() loses everything still buffered, and
 * so does any output file left for exit() to close. Verified with a bare
 * test program: printf() + exit(0) writes nothing once gdal_w64.dll is
 * loaded, and everything with an atexit(fflush(NULL)) handler.
 *
 * _crt_atexit() registers in the process-wide atexit table that exit()
 * runs (a plain atexit() in a DLL would only run at DLL unload, too late),
 * so the flush happens before the process tears down its DLLs.
 *
 * That table outlives the DLL: when mbaux is loaded on demand and unloaded
 * again (GMT FreeLibrary's the mbsystem plugin, and mbaux with it, at the end
 * of a session), exit() would call the handler in unmapped code and crash.
 * So mbaux pins itself in the process once the handler is registered.
 */

#ifdef _WIN32

#include <stdio.h>
#include <windows.h>

_ACRTIMP int __cdecl _crt_atexit(void(__cdecl *function)(void));

static void __cdecl mb_exitflush_streams(void) {
	fflush(NULL);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void)instance;
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH) {
		HMODULE self;
		_crt_atexit(mb_exitflush_streams);
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
		                   (LPCWSTR)mb_exitflush_streams, &self);
	}
	return TRUE;
}

#endif
