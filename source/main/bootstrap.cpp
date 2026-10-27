#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include "animation-pool.h"
static const wchar_t* payloadFolder=L"\\NPCCompanions";
static HMODULE module;
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
extern "C" {uintptr_t originalExports[6]={};}
static void Fail(const wchar_t* message){MessageBoxW(nullptr,message,L"NPC Companion Bootstrap",MB_ICONERROR);ExitProcess(1);}
static BOOL CALLBACK Initialize(PINIT_ONCE,void*,void**) {
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);wcscat_s(system,L"\\dinput8.dll");HMODULE real=LoadLibraryW(system);if(!real)Fail(L"Windows DirectInput could not be loaded.");
 const char* names[]={"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"};
 for(int i=0;i<6;i++){originalExports[i]=(uintptr_t)GetProcAddress(real,names[i]);if(!originalExports[i])Fail(L"A Windows DirectInput export is missing.");}
 wchar_t path[MAX_PATH];GetModuleFileNameW(module,path,MAX_PATH);wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return TRUE;*slash=0;std::wstring root=std::wstring(path)+payloadFolder;
 // Vortex-friendly: the mod is on whenever this proxy and NPCCompanions\NPCCompanions.dll are deployed.
 if(GetFileAttributesW((root+L"\\NPCCompanions.dll").c_str())==INVALID_FILE_ATTRIBUTES)return TRUE;
 SetEnvironmentVariableW(L"NPCCOMPANIONS_BOOTSTRAP",L"1");
 HMODULE payload=LoadLibraryW((root+L"\\NPCCompanions.dll").c_str());if(!payload)Fail(L"Companion payload could not be loaded. The game has been stopped.");
 auto init=(DWORD(WINAPI*)(void*))GetProcAddress(payload,"Initialize");if(!init || init(nullptr)!=0)Fail(L"Companion initialization failed. Read NPCCompanions.log. The game has been stopped.");
 // Other DLL mods: load every DLL in Game\\dllMods (the Lazy Loader convention), so this proxy can replace it.
 std::wstring mods=std::wstring(path)+L"\\dllMods\\";WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW((mods+L"*.dll").c_str(),&fd);
 if(h!=INVALID_HANDLE_VALUE){do{if(!_wcsicmp(fd.cFileName,L"NPCCompanions.dll"))continue;LoadLibraryW((mods+fd.cFileName).c_str());}while(FindNextFileW(h,&fd));FindClose(h);}
 return TRUE;
}
extern "C" void EnsureReady(){if(!InitOnceExecuteOnce(&once,Initialize,nullptr,nullptr))Fail(L"DirectInput initialization failed.");}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void*){
 if(reason==DLL_PROCESS_ATTACH){
  module=h;DisableThreadLibraryCalls(h);
  // Static import DLL attach precedes executable startup and the animation
  // registry constructor. Only fixed, signature-checked instruction operands
  // are changed here: no allocation, DLL loading, hooks, or file writes.
  wchar_t exe[MAX_PATH],payload[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);const wchar_t* leaf=wcsrchr(exe,L'\\');
  if(leaf&&!_wcsicmp(leaf+1,L"DarkSoulsII.exe")){
   GetModuleFileNameW(h,payload,MAX_PATH);wchar_t* slash=wcsrchr(payload,L'\\');if(!slash)return FALSE;*slash=0;
   if(wcslen(payload)+wcslen(payloadFolder)+20>=MAX_PATH)return FALSE;
   wcscat_s(payload,payloadFolder);wcscat_s(payload,L"\\NPCCompanions.dll");
   if(GetFileAttributesW(payload)!=INVALID_FILE_ATTRIBUTES&&!ExpandAnimationPoolBeforeStartup((uintptr_t)GetModuleHandleW(nullptr)))return FALSE;
  }
 }
 return TRUE;
}
