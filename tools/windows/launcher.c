// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    (void)instance; (void)previous; (void)arguments; (void)show;
    wchar_t dir[32768], core[32768], runtime[32768], plugins[32768], bin[32768];
    DWORD n=GetModuleFileNameW(NULL,dir,32768); if(!n || n>=32768) return 1;
    wchar_t *slash=wcsrchr(dir,L'\\'); if(!slash) return 1; *slash=L'\0';
    if(swprintf(core,32768,L"%ls\\DSView-core.exe",dir)<0 || swprintf(runtime,32768,L"%ls\\runtime",dir)<0 || swprintf(plugins,32768,L"%ls\\runtime\\plugins",dir)<0 || swprintf(bin,32768,L"%ls\\runtime\\bin",dir)<0) return 1;
    DWORD size=GetEnvironmentVariableW(L"PATH",NULL,0);
    wchar_t *oldpath=(wchar_t*)calloc(size+1,sizeof(wchar_t));
    wchar_t *newpath=(wchar_t*)calloc(size+wcslen(bin)+3,sizeof(wchar_t));
    if(!oldpath || !newpath) return 1;
    if(size) GetEnvironmentVariableW(L"PATH",oldpath,size);
    swprintf(newpath,size+wcslen(bin)+3,L"%ls;%ls",bin,oldpath);
    SetEnvironmentVariableW(L"PATH",newpath); SetEnvironmentVariableW(L"PYTHONHOME",runtime);
    SetEnvironmentVariableW(L"PYTHONPATH",NULL); SetEnvironmentVariableW(L"QT_PLUGIN_PATH",plugins);
    SetEnvironmentVariableW(L"DSVIEW_TEST_ISOLATION",NULL); SetEnvironmentVariableW(L"DSVIEW_BENCH_LEGACY",NULL); SetEnvironmentVariableW(L"DSVIEW_BENCH_CHUNK_DELAY_MS",NULL);
    wchar_t *cmd=_wcsdup(GetCommandLineW()); STARTUPINFOW si={0}; PROCESS_INFORMATION pi={0}; si.cb=sizeof(si);
    if(!CreateProcessW(core,cmd,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) { MessageBoxW(NULL,L"Unable to start DSView-core.exe. Check that the runtime directory is installed.",L"DSView",MB_OK|MB_ICONERROR); return 1; }
    WaitForSingleObject(pi.hProcess,INFINITE); DWORD code=1; GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); free(cmd); free(oldpath); free(newpath); return (int)code;
}
