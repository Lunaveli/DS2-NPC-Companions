#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <cmath>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <deque>
#include <functional>
#include <tlhelp32.h>
#include <algorithm>
#include <d3d11.h>
#include <dxgi.h>
#include <dinput.h>
#include "MinHook.h"
#include "version.h"
#include "animation-pool.h"
#include "spawns.h"
#include "roster.h"
#include "profiles.h"
#include "herald-layout.h"
#include "fogwalls.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

// NPC Companions 0.10: up to three companions chosen from an in-game menu (F7).
// Each map has three companion spawners (9800-9802, cloned from Glencour's Heide's summon spawner). A slot's real
// event flag starts/stops its spawner. The DLL picks the slot's NPC by rewriting the spawner's NPC record
// (GeneratorRegistParam 98990000+slot) on disk and in memory, and keeps companions near the player by moving the
// spawner's position copies (and the saved position) to the player and respawning (see TeleportSlot).

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

static HMODULE selfModule;
static uintptr_t image;
static std::wstring root, assets, ini, logPath, tracePath;
static std::atomic<ULONGLONG> workerHeartbeat{0};
static std::atomic<int> areaMarker{0};
static SRWLOCK logLock=SRWLOCK_INIT;
static DWORD ownPid;

constexpr int SlotCount=3;
// Spawner flags: SpawnerFlags (spawner section) - unused flags in an existing bank (see prepare.py)
constexpr int RosterCount=(int)(sizeof(Roster)/sizeof(Roster[0]));
constexpr int MapCount=(int)(sizeof(CompanionSpawns)/sizeof(CompanionSpawns[0]));
constexpr uintptr_t GameManagerRva=0x16148f0;

static std::atomic<uintptr_t> flagManager{0};
static std::atomic<int> currentMap{-1};
static std::atomic<bool> menuOpen{false};
static std::atomic<bool> hideCompanionLabels{false};

static std::string JsonString(const char* text) {
 std::string out="\"";for(const unsigned char* p=(const unsigned char*)text;*p;p++) {
  if(*p=='"'||*p=='\\'){out+='\\';out+=(char)*p;}
  else if(*p=='\n')out+="\\n";else if(*p=='\r')out+="\\r";else if(*p=='\t')out+="\\t";
  else if(*p>=32)out+=(char)*p;
 }return out+"\"";
}
static int GameMapId(){__try{return image?*(int*)(image+0x156d1c4):0;}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
static int GameStep(){__try{uintptr_t gm=*(uintptr_t*)(image+GameManagerRva);return gm?*(int*)(gm+0x24ac):-1;}__except(EXCEPTION_EXECUTE_HANDLER){return -1;}}
static void TraceLocked(const char* type,const std::string& fields) {
 if(tracePath.empty())return;SYSTEMTIME t;GetSystemTime(&t);FILE* f=nullptr;_wfopen_s(&f,tracePath.c_str(),L"ab");
 if(f){fprintf(f,"{\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"tick_ms\":%llu,\"event\":\"%s\",\"map_id\":%d,\"asset_map_hint\":%d,\"checkpoint\":%d%s}\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetTickCount64(),type,GameMapId(),currentMap.load(),areaMarker.load(),fields.c_str());fclose(f);}
}
static void Trace(const char* type,const std::string& fields="") {AcquireSRWLockExclusive(&logLock);TraceLocked(type,fields);ReleaseSRWLockExclusive(&logLock);}
static void Log(const char* fmt,...) {
 char s[1024];va_list a;va_start(a,fmt);vsnprintf(s,sizeof(s),fmt,a);va_end(a);
 AcquireSRWLockExclusive(&logLock);
 FILE* f=nullptr;_wfopen_s(&f,logPath.c_str(),L"ab");
 if(f){fprintf(f,"[%llu] %s\r\n",GetTickCount64(),s);fclose(f);}
 TraceLocked("diagnostic",",\"message\":"+JsonString(s));
 ReleaseSRWLockExclusive(&logLock);
}
static bool Patch(uintptr_t at,const void* src,size_t size) {
 DWORD old,ignored;if(!VirtualProtect((void*)at,size,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)at,src,size);FlushInstructionCache(GetCurrentProcess(),(void*)at,size);
 return VirtualProtect((void*)at,size,old,&ignored)!=0;
}

// ---- Guarded memory access (game structures are torn down during loading screens) ----
static bool ReadPtr(uintptr_t at,uintptr_t& out){__try{out=*(uintptr_t*)at;return true;}__except(EXCEPTION_EXECUTE_HANDLER){out=0;return false;}}
static bool ReadI32(uintptr_t at,int& out){__try{out=*(int*)at;return true;}__except(EXCEPTION_EXECUTE_HANDLER){out=0;return false;}}
static bool ReadVec(uintptr_t at,float* v){__try{v[0]=((float*)at)[0];v[1]=((float*)at)[1];v[2]=((float*)at)[2];return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool WriteVec(uintptr_t at,const float* v){__try{((float*)at)[0]=v[0];((float*)at)[1]=v[1];((float*)at)[2]=v[2];return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool WriteI32(uintptr_t at,int v){__try{*(int*)at=v;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool WriteI32x3(uintptr_t at,const int* v){__try{((int*)at)[0]=v[0];((int*)at)[1]=v[1];((int*)at)[2]=v[2];return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static bool ReadMatrix(uintptr_t at,float* m){__try{memcpy(m,(void*)at,64);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static float Distance(const float* a,const float* b){float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return sqrtf(x*x+y*y+z*z);}
static bool UserPointer(uintptr_t p){return p>=0x10000 && p<=0x7fffffffffffULL;}
static void TraceLoadingResources() {
 uintptr_t gm=0,cm=0,registry=0,node=0;int count=-1;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x18,cm)||!cm||!ReadPtr(cm+0x830,registry)||!registry||!ReadI32(registry+0x18,count))return;
 char b[160];snprintf(b,sizeof(b),",\"character_resource_count\":%d,\"loading_waits_for_resources\":%s,\"entries\":[",count,count>1?"true":"false");std::string out=b;
 if(ReadPtr(registry+0x10,node))for(int i=0;node&&i<128;i++) {
  int id=0,packed=0;uintptr_t next=0;if(!ReadI32(node+0x14,id)||!ReadI32(node+0x10,packed))break;
  snprintf(b,sizeof(b),"%s{\"id\":%d,\"references\":%d,\"state\":%d}",i?",":"",id,packed&255,(packed>>8)&63);out+=b;
  if(!ReadPtr(node+0x60,next)||next==node)break;node=next;
 }
 out+="]";Trace("loading_resources",out);
}

static uintptr_t PlayerCtrl() {
 uintptr_t gm=0,player=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0xd0,player))return 0;
 return player;
}

// ---- Event flags: mirrors DarkSoulsII.exe+0x474230 (bank=id/10000 in a 31-bucket table, MSB-first bits) ----
static int SetRealFlag(uint32_t id,bool on) {
 uintptr_t manager=flagManager.load();if(!manager)return -1;
 uintptr_t host=PlayerCtrl();int hostHp=0;if(!host || (on && (!ReadI32(host+0x168,hostHp)||hostHp<=0)))return -6;
 if(on && GameStep()!=30)return -7;
 uint32_t bank=id/10000,bit=id%10000,bucket=(bank*0x89)%31;
 __try {
  uintptr_t entry=*(uintptr_t*)(manager+0x20+bucket*8);
  int guard=0;while(entry && *(uint32_t*)(entry+0xc)!=bank && guard++<256)entry=*(uintptr_t*)(entry+0x10);
  if(guard>=256)return -5;
  if(!entry)return -2;
  if((bit>>3)>=*(uint32_t*)(entry+8))return -3;
  char* byte=*(char**)entry+(bit>>3);char mask=(char)(1<<(7-(bit&7)));
  if(on)_InterlockedOr8(byte,mask);else _InterlockedAnd8(byte,(char)~mask);
  return 1;
 } __except(EXCEPTION_EXECUTE_HANDLER){return -4;}
}

static std::atomic<bool> openFog{false};   // [world] openFog: open ordinary area fog walls (not boss doors)
static std::atomic<float> enemyHpMultiplier{1.0f},enemyHpSelected{3.0f};
static std::atomic<bool> enemyHpEnabled{false};
static bool ValidHpMultiplier(float value){return std::isfinite(value)&&value>=1.0f&&value<=99.0f;}
static float HpMultiplierSetting(float value,float fallback=1.0f){return ValidHpMultiplier(value)?roundf(value*10.0f)/10.0f:fallback;}
static void SetWorldHpOptions(bool enabled,float selected,bool save){
 selected=HpMultiplierSetting(selected);enemyHpSelected=selected;enemyHpEnabled=enabled;enemyHpMultiplier=enabled?selected:1.0f;
 if(save){wchar_t text[32];swprintf_s(text,L"%.1f",selected);WritePrivateProfileStringW(L"world",L"enemyHpSelected",text,ini.c_str());WritePrivateProfileStringW(L"world",L"enemyHpEnabled",enabled?L"1":L"0",ini.c_str());swprintf_s(text,L"%.1f",enemyHpMultiplier.load());WritePrivateProfileStringW(L"world",L"enemyHpMultiplier",text,ini.c_str());}
}
static void LoadWorldHpOptions(){
 wchar_t text[64];GetPrivateProfileStringW(L"world",L"enemyHpMultiplier",L"1",text,64,ini.c_str());float legacy=HpMultiplierSetting((float)_wtof(text));
 wchar_t fallback[32];swprintf_s(fallback,L"%.1f",legacy>1?legacy:3.0f);GetPrivateProfileStringW(L"world",L"enemyHpSelected",fallback,text,64,ini.c_str());
 SetWorldHpOptions(GetPrivateProfileIntW(L"world",L"enemyHpEnabled",legacy>1?1:0,ini.c_str())!=0,HpMultiplierSetting((float)_wtof(text),3.0f),false);
}
#include "player-options.h"
static void OpenAreaFogWalls(const char* why) {
 int opened=0,missing=0;
 for(uint32_t f:AreaFogPassFlags){int r=SetRealFlag(f,true);if(r==1)opened++;else missing++;}
 Log("%s: area fog wall pass flags set %d (%d belong to areas not loaded now)",why,opened,missing);
}
// ---- Hooks: asset override (map tracking) and flag manager capture ----
struct GameString {wchar_t* text;void* unknown;uint64_t length,capacity;};
using PathFn=uintptr_t(*)(uintptr_t,GameString*);
using GrowFn=void(*)(GameString*,size_t,size_t);
using FlagFn=uint8_t(*)(uintptr_t,uint32_t);
static PathFn originalPath;
static FlagFn originalFlag;
static GrowFn grow;
static bool BlockedCompanionFlag(uint32_t id);
static uintptr_t Redirect(uintptr_t owner,GameString* path) {
 if(path && path->text && path->length>0 && path->length<MAX_PATH) {
  const wchar_t* rel=nullptr;
  if(wcsncmp(path->text,L"gamedata:/",10)==0)rel=path->text+10;
  else if(wcsncmp(path->text,L"title:/",7)==0)rel=path->text+7;
  if(rel && _wcsnicmp(rel,L"param/GeneratorParam_",21)==0) {
   for(int i=0;i<MapCount;i++)
    if(_wcsnicmp(rel+21,CompanionSpawns[i].map,12)==0 && currentMap.exchange(i)!=i)Log("Map: %ls",CompanionSpawns[i].map);
  }
  if(rel && !wcsstr(rel,L"..") && !wcschr(rel,L':') && *rel!=L'/' && *rel!=L'\\') {
   std::wstring replacement=assets+L"\\"+rel;
   DWORD a=GetFileAttributesW(replacement.c_str());
   if(a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY)) {
    if(path->capacity<replacement.size()+1)grow(path,replacement.size()+1,(size_t)path->length);
    if(path->text && path->capacity>=replacement.size()+1) {
     memcpy(path->text,replacement.c_str(),(replacement.size()+1)*sizeof(wchar_t));path->length=replacement.size();
    }
   }
  }
 }
 return originalPath(owner,path);
}
static uint8_t GetFlag(uintptr_t manager,uint32_t id) {
 if(flagManager.load(std::memory_order_relaxed)!=manager){flagManager=manager;Log("Event flag manager: 0x%llx",(unsigned long long)manager);}
 if(BlockedCompanionFlag(id))return 0;
 return originalFlag(manager,id);
}

// ---- Param files in memory: header type name at +0x0C, row count (u16) at +0x0A, 24-byte rows from +0x40 ----
// Row: u32 id, u32 pad, u64 data offset (relative to the file start), u64 name offset.
static uintptr_t ParamRowData(uintptr_t base,uint32_t id) {
 __try {
  uint16_t rows=*(uint16_t*)(base+0x0A);
  for(uint32_t i=0;i<rows;i++) {
   uintptr_t e=base+0x40+i*24;
   if(*(uint32_t*)e!=id)continue;
   uint64_t off=*(uint64_t*)(e+8);
   return off<0x4000000?base+off:(uintptr_t)off;   // relative offset, or already fixed up to a pointer
  }
 } __except(EXCEPTION_EXECUTE_HANDLER){}
 return 0;
}

// ---- Spawners: 6 per slot per map, each with its own flag. j<4: GeneratorLocation 9800+slot*4+j (the anchors the
// tracked companion uses); j=4,5: group-only spawners 9812+slot*2+(j-4), copies of the slot's spawners 0 and 1 ----
// A spawner belongs to the part of the map it stands in; spread-out spawners keep one loaded near the player. Only
// one spawner per slot is switched on at a time (so there is only ever one character per slot).
constexpr int SpawnerCount=SlotCount*SpawnersPerSlot;
constexpr int PrimarySpawners=4;
constexpr uint32_t SpawnerFlags[SlotCount][SpawnersPerSlot]={{104899,104787,104803,104835,104785,104881},{104959,104855,104857,104859,104887,104897},{104961,104861,104867,104869,104989,104997}};
// Index into CompanionSpawns[].spawner and scan arrays; the generator ID is 9800+index.
static int SpawnerIndex(int k,int j){return j<PrimarySpawners?k*PrimarySpawners+j:SlotCount*PrimarySpawners+k*(SpawnersPerSlot-PrimarySpawners)+j-PrimarySpawners;}
static bool SlotOwnsGenerator(int k,unsigned id){for(int j=0;j<SpawnersPerSlot;j++)if(id==9800u+SpawnerIndex(k,j))return true;return false;}
// Where each spawner copy was last moved to (per map): after a move the copy no longer holds the spawns.h position.
static float movedSpot[SpawnerCount][3];static bool hasMovedSpot[SpawnerCount];
// spawn[s]: copies in world coordinates; local[s]: copies in map-local coordinates (world - MapOffset).
struct ScanResult {std::vector<uintptr_t> regist,chr,generator;std::vector<uintptr_t> spawn[SpawnerCount],local[SpawnerCount];};
// Cooperative read-only discovery: inspect typed PARAM headers, never arbitrary coordinate matches.
// The old scan could monopolize the companion worker for 30 seconds and mistake unrelated vectors for spawners.
static uintptr_t scanCursor=0x10000;
static bool ScanMemory(ScanResult& r,int map) {
 ULONG_PTR stackLow=0,stackHigh=0;GetCurrentThreadStackLimits(&stackLow,&stackHigh);
 ULONGLONG until=GetTickCount64()+6;size_t examined=0;MEMORY_BASIC_INFORMATION mbi;
 while(VirtualQuery((void*)scanCursor,&mbi,sizeof(mbi))) {
  uintptr_t begin=(uintptr_t)mbi.BaseAddress,end=begin+mbi.RegionSize;
  if(end<=scanCursor){scanCursor=0x10000;return true;}
  bool readable=mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
    (mbi.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE)) && mbi.Type==MEM_PRIVATE && !(begin<stackHigh&&end>stackLow);
  if(!readable){scanCursor=end;continue;}
  uintptr_t stop=(std::min)(end,scanCursor+0x10000);
  __try {
   for(uintptr_t q=scanCursor;q+40<=stop;q+=4) {
    uint32_t tag=*(uint32_t*)q;if(q<begin+0x0c)continue;
    uintptr_t table=q-0x0c;
    if(tag==0x454e4547u && memcmp((void*)q,"GENERATOR_REGIST_PARAM",22)==0 && r.regist.size()<64)r.regist.push_back(table);
    else if(tag==0x454e4547u && memcmp((void*)q,"GENERATOR_PARAM",16)==0 && r.generator.size()<64)r.generator.push_back(table);
    else if(tag==0x5f524843u && memcmp((void*)q,"CHR_PARAM",10)==0 && r.chr.size()<16)r.chr.push_back(table);
    else if(map>=0 && tag==0x454e4547u && memcmp((void*)q,"GENERATOR_LOCATION_PARAM",25)==0) {
     const auto& cs=CompanionSpawns[map];
     for(int i=0;i<SpawnerCount;i++) {
      uintptr_t row=ParamRowData(table,9800+i);float v[3];if(!row||!ReadVec(row,v))continue;
      float expected[3]={cs.spawner[i][0]-cs.ox,cs.spawner[i][1]-cs.oy,cs.spawner[i][2]-cs.oz};
      float moved[3]={movedSpot[i][0]-cs.ox,movedSpot[i][1]-cs.oy,movedSpot[i][2]-cs.oz};
      if((Distance(v,expected)<0.008f || (hasMovedSpot[i]&&Distance(v,moved)<0.008f)) && r.local[i].size()<16)r.local[i].push_back(row);
     }
    }
   }
  } __except(EXCEPTION_EXECUTE_HANDLER){}
  // Retain overlap so a PARAM header straddling a slice is examined next time.
  scanCursor=stop<end?stop-40:end;examined+=0x10000;
  if(examined>=0x4000000 || GetTickCount64()>=until)return false;
 }
 scanCursor=0x10000;
 Log("Typed PARAM scan complete: %d regist tables, %d CHR tables",(int)r.regist.size(),(int)r.chr.size());
 return true;
}

// ---- Slots ----
struct Slot {
 int npc=0;bool active=false;
 unsigned triedSpawners=0;   // failed candidates in this recovery round
 int matchCount=0,placementAttempts=0;
 int spawner=0;   // which of the slot's 4 spawners is switched on
 uintptr_t ctrl=0;float pos[3]={};float distance=-1;int hp=0,maxHp=0;
 int autoTeleports=0,missingRetries=0,stuckTeleports=0;ULONGLONG nextTeleport=0,missingSince=0,stuckSince=0;float bestDistance=0;
 ULONGLONG respawnedAt=0;int keepHp=0;uintptr_t oldCtrl=0;   // HP carried over a respawn, applied to the next character
 bool observedMovement=false;float movementAnchor[3]={};float lastHostY=0;ULONGLONG motionAt=0,verticalAt=0,farAt=0;
 bool needsPlacement=false;uintptr_t recallCtrl=0;ULONGLONG recallAt=0;
 bool loadChecked=false;   // first appearance in this area checked (save loads can spawn at the default spot)
 // Group spawns: the other members, each from another of the slot's spawners; groupHp/groupMax include ctrl.
 uintptr_t extras[SpawnersPerSlot-1]={};int extraHp[SpawnersPerSlot-1]={};ULONGLONG extraMoveAt[SpawnersPerSlot-1]={};int extraCount=0,groupHp=0,groupMax=0;
};
static Slot slots[SlotCount];
static std::atomic<int> nextLaunchNpc[SlotCount]{{-1},{-1},{-1}};
static int startupHigh[SlotCount]={},startupLow[SlotCount]={};
static SRWLOCK slotLock=SRWLOCK_INIT;
static ScanResult scan;static bool scanned=false;
static std::vector<uintptr_t> enemyTables;   // the game keeps more than one copy of EnemyParam (CHR_PARAM) in memory
static std::mutex queueLock;static std::deque<std::function<void()>> queue;
static void Enqueue(std::function<void()> f){std::lock_guard<std::mutex> g(queueLock);queue.push_back(std::move(f));}

static float JournalNumber(float value){return std::isfinite(value)?value:0.0f;}
static bool HeraldTalking(int k);
static int NpcResourceState(int npc);
static void Snapshot(const char* event) {
 uintptr_t host=PlayerCtrl();int hp=0;float pp[3]={};if(host){ReadI32(host+0x168,hp);ReadVec(host+0x90,pp);}for(float& v:pp)v=JournalNumber(v);
 char b[512];snprintf(b,sizeof(b),",\"host\":{\"ptr\":\"0x%llx\",\"hp\":%d,\"pos\":[%.3f,%.3f,%.3f]},\"game_step\":%d,\"worker_age_ms\":%llu,\"slots\":[",(unsigned long long)host,hp,pp[0],pp[1],pp[2],GameStep(),GetTickCount64()-workerHeartbeat.load());
 std::string fields=b;
 for(int k=0;k<SlotCount;k++) {const Slot& v=slots[k];snprintf(b,sizeof(b),"%s{\"slot\":%d,\"active\":%s,\"actor\":\"0x%llx\",\"hp\":%d,\"max_hp\":%d,\"distance\":%.3f,\"pos\":[%.3f,%.3f,%.3f],\"candidates\":%d,\"spawner\":%d,\"missing_retries\":%d,\"npc\":",k?",":"",k+1,v.active?"true":"false",(unsigned long long)v.ctrl,v.hp,v.maxHp,JournalNumber(v.distance),JournalNumber(v.pos[0]),JournalNumber(v.pos[1]),JournalNumber(v.pos[2]),v.matchCount,v.spawner+1,v.missingRetries);fields+=b;fields+=JsonString(Roster[v.npc].name);fields+='}';}
 fields+=']';fields+=",\"npc_loading\":[";for(int k=0;k<SlotCount;k++){int requested=nextLaunchNpc[k].load();snprintf(b,sizeof(b),"%s{\"slot\":%d,\"requested\":%d,\"resource_state\":%d,\"herald_talking\":%s}",k?",":"",k+1,requested,NpcResourceState(requested),HeraldTalking(k)?"true":"false");fields+=b;}fields+=']';Trace(event,fields);
}
static bool IsEnemyRow(uintptr_t param,int enemy){for(uintptr_t t:enemyTables)if(param && ParamRowData(t,(uint32_t)enemy)==param)return true;return false;}
static bool IsHerald(int npc){return npc>=0&&npc<RosterCount&&Roster[npc].enemy==990712;}
// Use the active gameplay map, never the last streamed asset-directory hint.
static bool HeraldBlockedHere(int npc){return IsHerald(npc)&&GameMapId()==0x0a040000;}
static bool BlockedCompanionFlag(uint32_t id){
 if(GameMapId()!=0x0a040000)return false;
 for(int k=0;k<SlotCount;k++)if(IsHerald(slots[k].npc)){
  if(id==HeraldEnabledFlags[k])return true;
  for(uint32_t f:SpawnerFlags[k])if(id==f)return true;
 }
 return false;
}
static bool HeraldTalking(int k){uintptr_t manager=flagManager.load();return manager&&originalFlag&&originalFlag(manager,HeraldBusyFlags[k])!=0;}
static bool HeraldServiceBusy(int k){uintptr_t manager=flagManager.load();return !manager||!originalFlag||HeraldTalking(k)||originalFlag(manager,HeraldLevelFlags[k])!=0;}
static void ProfileLogicFlags(int k,int* flags){flags[0]=flags[1]=flags[2]=flags[3]=0;if(IsHerald(slots[k].npc)){flags[0]=HeraldLevelFlags[k];flags[1]=104020149;flags[3]=HeraldBusyFlags[k];}}
static const char* NpcDisplayName(int npc){
 // Keep the historical names as settings keys: Thomas and Melinda each have
 // two distinct loadouts. Only the visible label drops the old role suffix.
 static const std::vector<std::string> names=[](){std::vector<std::string> result;for(const auto& n:Roster){std::string name=n.name;const std::string suffix=" (Invader)";if(name.size()>=suffix.size()&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0)name.resize(name.size()-suffix.size());result.push_back(name);}return result;}();
 return npc>=0&&npc<RosterCount?names[npc].c_str():"Unknown NPC";
}

static void SaveSlots() {
 for(int k=0;k<SlotCount;k++) {
  wchar_t key[16],value[128];
  int requested=nextLaunchNpc[k].load();if(requested<0)requested=slots[k].npc;
  swprintf_s(key,L"npc%d",k+1);MultiByteToWideChar(CP_UTF8,0,Roster[requested].name,-1,value,128);WritePrivateProfileStringW(L"slots",key,value,ini.c_str());
  swprintf_s(key,L"active%d",k+1);WritePrivateProfileStringW(L"slots",key,slots[k].active?L"1":L"0",ini.c_str());
 }
}
static void LoadSlots() {
 const char* defaults[SlotCount]={"Masterless Glencour","Devotee Scarlett","Abbess Feeva"};
 for(int k=0;k<SlotCount;k++) {
  wchar_t key[16],value[128];char name[128];
  swprintf_s(key,L"npc%d",k+1);GetPrivateProfileStringW(L"slots",key,L"",value,128,ini.c_str());WideCharToMultiByte(CP_UTF8,0,value,-1,name,128,nullptr,nullptr);
  if(!name[0])strcpy_s(name,defaults[k]);
  slots[k].npc=0;for(int i=0;i<RosterCount;i++)if(strcmp(Roster[i].name,name)==0)slots[k].npc=i;
  swprintf_s(key,L"active%d",k+1);slots[k].active=GetPrivateProfileIntW(L"slots",key,0,ini.c_str())!=0;
 }
}

// Rewrite row 98990000+slot in every companion GeneratorRegistParam file, so the next map load spawns that NPC.
static int PatchRegistFiles(int k) {
 const RosterEntry& n=Roster[slots[k].npc];const int ids[3]={n.enemy,n.high,n.low};int patched=0;
 WIN32_FIND_DATAW fd;std::wstring pattern=assets+L"\\param\\generatorregistparam_*.param";
 HANDLE h=FindFirstFileW(pattern.c_str(),&fd);if(h==INVALID_HANDLE_VALUE)return 0;
 do {
  std::wstring path=assets+L"\\param\\"+fd.cFileName;
  FILE* f=nullptr;if(_wfopen_s(&f,path.c_str(),L"r+b")||!f)continue;
  std::vector<uint8_t> b;fseek(f,0,SEEK_END);b.resize(ftell(f));fseek(f,0,SEEK_SET);fread(b.data(),1,b.size(),f);
  uint16_t rows=b.size()>0x40?*(uint16_t*)&b[0x0A]:0;
  for(uint32_t i=0;i<rows && 0x40+i*24+24<=b.size();i++) {
   size_t e=0x40+i*24;if(*(uint32_t*)&b[e]!=98990000u+k)continue;
   uint64_t off=*(uint64_t*)&b[e+8];if(off+12>b.size())break;
   fseek(f,(long)off,SEEK_SET);fwrite(ids,4,3,f);patched++;break;
  }
  fclose(f);
 } while(FindNextFileW(h,&fd));
 FindClose(h);return patched;
}
static int PatchProfileFiles(int k) {
 int boot=ProfileBoot(Roster[slots[k].npc].enemy),patched=0;WIN32_FIND_DATAW fd;
 int logicFlags[4];ProfileLogicFlags(k,logicFlags);
 HANDLE h=FindFirstFileW((assets+L"\\param\\generatorparam_*.param").c_str(),&fd);if(h==INVALID_HANDLE_VALUE)return 0;
 do {
  FILE* f=nullptr;std::wstring path=assets+L"\\param\\"+fd.cFileName;if(_wfopen_s(&f,path.c_str(),L"r+b")||!f)continue;
  fseek(f,0,SEEK_END);long size=ftell(f);if(size<0x40){fclose(f);continue;}fseek(f,0,SEEK_SET);std::vector<unsigned char> b(size);bool ok=fread(b.data(),1,b.size(),f)==b.size();
  if(ok){unsigned count=*(uint16_t*)&b[0xa];for(unsigned i=0;i<count&&0x40+i*24+24<=b.size();i++){
   size_t e=0x40+i*24;unsigned id=*(uint32_t*)&b[e];if(!SlotOwnsGenerator(k,id))continue;
   uint64_t offset=*(uint64_t*)&b[e+8];if(offset>b.size()||BootLowOffset+4>b.size()-offset)continue;
   if(LogicEventFlagsOffset+16>b.size()-offset)continue;
   fseek(f,(long)(offset+BootLowOffset),SEEK_SET);if(fwrite(&boot,4,1,f)==1)patched++;
   fseek(f,(long)(offset+LogicEventFlagsOffset),SEEK_SET);fwrite(logicFlags,4,4,f);
  }}fclose(f);
 }while(FindNextFileW(h,&fd));FindClose(h);return patched;
}
struct RegistEdit {uintptr_t row;int original[3],applied[3];};
static std::vector<RegistEdit> registEdits;
static bool ReadIds(uintptr_t row,int* ids){return ReadI32(row,ids[0])&&ReadI32(row+4,ids[1])&&ReadI32(row+8,ids[2]);}
static bool PatchRegistRow(uintptr_t row,const int* ids) {
 int before[3];if(!ReadIds(row,before))return false;
 auto it=std::find_if(registEdits.begin(),registEdits.end(),[&](const RegistEdit& e){return e.row==row;});
 if(it==registEdits.end()) {
  if(memcmp(before,ids,sizeof(before))==0)return true;
  RegistEdit e;e.row=row;memcpy(e.original,before,sizeof(before));memcpy(e.applied,ids,sizeof(before));registEdits.push_back(e);it=registEdits.end()-1;
 }
 else if(memcmp(before,it->applied,sizeof(before))!=0) {
  Log("Registration row 0x%llx changed externally; refusing overwrite",(unsigned long long)row);return false;
 }
 if(!WriteI32x3(row,ids))return false;memcpy(it->applied,ids,sizeof(before));return true;
}
// Four generators retain resource references under the IDs read at map load.
// Restore those exact IDs before native map teardown releases its references.
static int RestoreRegistMemory() {
 int restored=0;
 for(const auto& e:registEdits) {
  int actual[3];bool match=ReadIds(e.row,actual)&&memcmp(actual,e.applied,sizeof(actual))==0;
  bool ok=match&&WriteI32x3(e.row,e.original);if(ok)restored++;
  char b[320];snprintf(b,sizeof(b),",\"row\":\"0x%llx\",\"original_enemy\":%d,\"selected_enemy\":%d,\"restored\":%s",(unsigned long long)e.row,e.original[0],e.applied[0],ok?"true":"false");Trace("registration_restored",b);
 }
 registEdits.clear();return restored;
}
static int PatchRegistMemory(int k) {
 uintptr_t host=PlayerCtrl();int hp=0;if(GameStep()!=30||!host||!ReadI32(host+0x168,hp)||hp<=0)return 0;
 const RosterEntry& n=Roster[slots[k].npc];const int ids[3]={n.enemy,n.high,n.low};int patched=0;
 for(uintptr_t t:scan.regist){uintptr_t row=ParamRowData(t,98990000u+k);if(row && PatchRegistRow(row,ids))patched++;}
 int boot=ProfileBoot(n.enemy),flags[4];ProfileLogicFlags(k,flags);for(uintptr_t t:scan.generator)for(int j=0;j<SpawnersPerSlot;j++){uintptr_t row=ParamRowData(t,9800+SpawnerIndex(k,j));if(row){WriteI32(row+BootLowOffset,boot);for(int i=0;i<4;i++)WriteI32(row+LogicEventFlagsOffset+i*4,flags[i]);}}
 return patched;
}
#include "resources.h"
#include "world-health.h"
#include "companion-health.h"
static int NpcResourceState(int npc){return npc>=0&&npc<RosterCount?resourceLeases[npc].state.load():0;}

static bool HasCopies(int s){return !scan.spawn[s].empty()||!scan.local[s].empty();}
static bool SlotHasCopies(int k){for(int j=0;j<PrimarySpawners;j++)if(HasCopies(SpawnerIndex(k,j)))return true;return false;}
static void MoveCopies(int s,const float* spot) {
 int map=currentMap.load();if(map<0)return;
 const auto& cs=CompanionSpawns[map];
 const float* expected=hasMovedSpot[s]?movedSpot[s]:cs.spawner[s];
 float localExpected[3]={expected[0]-cs.ox,expected[1]-cs.oy,expected[2]-cs.oz};
 float localSpot[3]={spot[0]-cs.ox,spot[1]-cs.oy,spot[2]-cs.oz};
 auto move=[&](std::vector<uintptr_t>& copies,const float* from,const float* to){
  copies.erase(std::remove_if(copies.begin(),copies.end(),[&](uintptr_t a){
   float v[3];return !ReadVec(a,v)||Distance(v,from)>0.008f||!WriteVec(a,to);
  }),copies.end());
 };
 move(scan.spawn[s],expected,spot);move(scan.local[s],localExpected,localSpot);
 if(HasCopies(s)){memcpy(movedSpot[s],spot,12);hasMovedSpot[s]=true;}
}
// The slot's spawner to use now: the nearest one to the player that has a findable copy (else just the nearest).
static int PickSpawner(int k,const float* pp) {
 int map=currentMap.load();if(map<0)return 0;
 int best=-1,bestAny=0;float bestD=1e30f,bestAnyD=1e30f;
 for(int j=0;j<PrimarySpawners;j++) {
  int s=SpawnerIndex(k,j);float d=Distance(CompanionSpawns[map].spawner[s],pp);
  if(d<bestAnyD){bestAnyD=d;bestAny=j;}
  if(HasCopies(s) && !(slots[k].triedSpawners&(1u<<j)) && d<bestD){bestD=d;best=j;}
 }
 if(best>=0)return best;
 // All known candidates tried: start another round, never permanently abandon an active slot.
 if(slots[k].triedSpawners){slots[k].triedSpawners=0;return PickSpawner(k,pp);}
 return bestAny;
}
static void GroupFlagsCleared(int k);
static int SlotFlagsOff(int k){int cleared=0;for(int j=0;j<SpawnersPerSlot;j++)if(SetRealFlag(SpawnerFlags[k][j],false)==1)cleared++;GroupFlagsCleared(k);return cleared;}
// Group members spread around the player instead of stacking on the tracked character.
static void GroupSpot(int k,int e,const float* pp,float* spot){float a=0.9f*k+1.6f*(e+1);spot[0]=pp[0]+1.8f*cosf(a);spot[1]=pp[1]+0.3f;spot[2]=pp[2]+1.8f*sinf(a);}
// Group members come from the slot's other spawners, switched on one at a time. A spawner whose character was
// switched off goes dormant until the area reloads (generator state 0, as after a bonfire rest), and spawners of
// one slot switched on together left only one character alive, so each member is confirmed before the next.
struct GroupSpawnState {unsigned on=0,dormant=0,used=0;int pending=-1,before=0;ULONGLONG pendingSince=0,nextAt=0;};
static GroupSpawnState groupSpawn[SlotCount];
constexpr ULONGLONG GroupSpawnTimeout=8000,GroupSpawnGap=1500;
// Every flag of the slot went off: spawners that produced a member are dormant for the rest of this area.
static void GroupFlagsCleared(int k){GroupSpawnState& g=groupSpawn[k];g.dormant|=g.used;g.on=0;g.used=0;g.pending=-1;g.nextAt=GetTickCount64()+2500;}
static void GroupAreaReset(int k){groupSpawn[k]=GroupSpawnState();groupSpawn[k].nextAt=GetTickCount64()+2500;}
static int GroupOnCount(int k){int n=0;for(int j=0;j<SpawnersPerSlot;j++)if((groupSpawn[k].on>>j)&1&&!((groupSpawn[k].dormant>>j)&1))n++;return n;}
// Menu changes: a smaller group switches off its newest members' spawners; growth happens in UpdateGroupSpawns.
static void ApplyGroupFlags(int k,const float*) {
 GroupSpawnState& g=groupSpawn[k];int want=slots[k].active?GroupCount(k)-1:0;
 for(int j=SpawnersPerSlot-1;j>=0&&GroupOnCount(k)>want;j--)if((g.on>>j)&1&&!((g.dormant>>j)&1)){SetRealFlag(SpawnerFlags[k][j],false);g.on&=~(1u<<j);if((g.used>>j)&1){g.dormant|=1u<<j;g.used&=~(1u<<j);}if(g.pending==j)g.pending=-1;}
 Log("Slot %d (%s) group size %d: %d extra spawners on",k+1,Roster[slots[k].npc].name,want+1,GroupOnCount(k));
}
static void UpdateGroupSpawns(const float* pp,ULONGLONG now) {
 int map=currentMap.load();if(map<0||!scanned)return;
 for(int k=0;k<SlotCount;k++) {
  GroupSpawnState& g=groupSpawn[k];Slot& s=slots[k];int want=GroupCount(k)-1;
  if(!s.active||want<=0||HeraldBlockedHere(s.npc)||nextLaunchNpc[k].load()>=0)continue;
  int present=s.extraCount;
  if(g.pending>=0) {
   if(present>g.before){g.used|=1u<<g.pending;Log("Slot %d group member from spawner %d confirmed (%d of %d)",k+1,g.pending+1,present+1,want+1);g.pending=-1;g.nextAt=now+GroupSpawnGap;}
   else if(now-g.pendingSince>=GroupSpawnTimeout){g.dormant|=1u<<g.pending;Log("Slot %d group spawner %d produced no member in %llu s; treated as dormant until the area reloads",k+1,g.pending+1,GroupSpawnTimeout/1000);g.pending=-1;g.nextAt=now;}
   continue;
  }
  // Wait for the tracked companion first; never enable more spawners than members wanted.
  if(!s.ctrl||s.hp<=0||now<g.nextAt||GroupOnCount(k)>=want)continue;
  int best=-1;float bestD=1e30f;
  for(int j=0;j<SpawnersPerSlot;j++){if(j==s.spawner||((g.on|g.dormant)>>j)&1)continue;float d=Distance(CompanionSpawns[map].spawner[SpawnerIndex(k,j)],pp);if(d<bestD){bestD=d;best=j;}}
  if(best<0){if(!(g.nextAt>now)){Log("Slot %d group: %d of %d members; no spare spawner left in this area (rest at a bonfire to reset them)",k+1,present+1,want+1);}g.nextAt=now+60000;continue;}
  if(SetRealFlag(SpawnerFlags[k][best],true)==1){g.on|=1u<<best;g.pending=best;g.before=present;g.pendingSince=now;Log("Slot %d group: spawner %d on for member %d of %d",k+1,best+1,present+2,want+1);}
  else g.nextAt=now+GroupSpawnGap;
 }
}
// Generator positions stay at their authored map anchors. The engine may already have cached
// the original transforms; writing the PARAM seed is not a live-spawner move.
// A spawned actor is recalled through RequestPosition instead.
static const char* RecallPolicy(float distance,float heightGap,ULONGLONG idleMs,ULONGLONG stableHeightMs,ULONGLONG farMs) {
 if(heightGap>8.0f && stableHeightMs>=3000)return "Auto (vertical transition)";
 if(distance>60.0f && farMs>=3000)return "Auto (emergency distance)";
 if(distance>40.0f && idleMs>=8000)return "Auto (far, stationary)";
 if(distance>18.0f && idleMs>=12000)return "Auto (stuck, stationary)";
 return nullptr;
}
static const char* ObserveRecall(Slot& s,const float* pp,ULONGLONG now) {
 if(!s.observedMovement){s.observedMovement=true;memcpy(s.movementAnchor,s.pos,12);s.motionAt=now;s.lastHostY=pp[1];s.verticalAt=now;s.farAt=now;}
 if(Distance(s.pos,s.movementAnchor)>0.6f){memcpy(s.movementAnchor,s.pos,12);s.motionAt=now;}
 if(fabsf(pp[1]-s.lastHostY)>0.3f){s.lastHostY=pp[1];s.verticalAt=now;}
 if(s.distance<=60.0f)s.farAt=now;
 return RecallPolicy(s.distance,fabsf(pp[1]-s.pos[1]),now-s.motionAt,now-s.verticalAt,now-s.farAt);
}
// Request a live actor move through its position controller; keep the spawner/actor ownership intact.
// ChrCtrl +0xc8 -> PositionCtrl: target vec4 +0, orientation vec4 +0x10, request bits +0xfc.
static bool RequestPosition(uintptr_t actor,const float* target) {
 uintptr_t pc=0;if(!UserPointer(actor)||!ReadPtr(actor+0xc8,pc)||!UserPointer(pc))return false;
 for(int i=0;i<3;i++)if(!std::isfinite(target[i])||fabsf(target[i])>100000)return false;
 __try {
  float w=*(float*)(actor+0x9c);float orientation[4];memcpy(orientation,(void*)(actor+0x80),16);
  if(!std::isfinite(w))return false;
  for(float f:orientation)if(!std::isfinite(f))return false;
  memcpy((void*)pc,target,12);*(float*)(pc+12)=w;memcpy((void*)(pc+0x10),orientation,16);
  _InterlockedOr8((char*)(pc+0xfc),3);
  return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#include "fall-guard.h"
// Respawn a slot next to the player: pick its nearest usable spawner, move that spawner's copies beside the player,
// switch the slot's flags off, then switch only that spawner's flag on.
static void TeleportSlot(int k,const char* why) {
 if(HeraldBlockedHere(slots[k].npc)){SlotFlagsOff(k);Log("%s slot %d: companion Herald blocked in Majula",why,k+1);return;}
 uintptr_t player=PlayerCtrl();float pp[3];
 int hostHp=0;if(!player||!ReadI32(player+0x168,hostHp)||hostHp<=0||GameStep()!=30||!ReadVec(player+0x90,pp)){Log("%s slot %d: player not loaded.",why,k+1);return;}
 float spot[3]={pp[0]+1.0f+0.8f*k,pp[1]+0.3f,pp[2]+1.0f-0.8f*k};
 if(slots[k].ctrl && slots[k].hp>0) {
  uintptr_t param=0;int hp=0;
  if(FallGuardHold(k,why))return;
  if(ReadPtr(slots[k].ctrl+0x38,param)&&IsEnemyRow(param,Roster[slots[k].npc].enemy)&&ReadI32(slots[k].ctrl+0x168,hp)&&hp>0 && RequestPosition(slots[k].ctrl,spot)) {
   FallGuardMoved(k,spot);
   slots[k].recallCtrl=slots[k].ctrl;slots[k].recallAt=GetTickCount64();slots[k].nextTeleport=GetTickCount64()+12000;
   for(int e=0;e<slots[k].extraCount;e++){float member[3];GroupSpot(k,e,pp,member);RequestPosition(slots[k].extras[e],member);}
   Log("%s slot %d: live position request to (%.1f, %.1f, %.1f); spawner left on",why,k+1,spot[0],spot[1],spot[2]);
  } else Log("%s slot %d: live position request unavailable; character left intact",why,k+1);
  return;
 }
 if(!scanned){Log("%s slot %d: waiting for typed PARAM discovery",why,k+1);return;}
 if(!slots[k].ctrl && slots[k].respawnedAt)slots[k].triedSpawners|=1u<<slots[k].spawner;
 else slots[k].triedSpawners=0;
 int j=PickSpawner(k,pp),s=SpawnerIndex(k,j);
 // Keep the selected spawner at its stable map anchor; placement follows actor creation.
 if(slots[k].ctrl && slots[k].hp>0){slots[k].keepHp=slots[k].hp;slots[k].oldCtrl=slots[k].ctrl;}
 slots[k].respawnedAt=GetTickCount64();
 SlotFlagsOff(k);Sleep(700);
 slots[k].spawner=j;
 // Loading can start during the flag-off interval. Let area initialization handle the next player.
 if(PlayerCtrl()!=player){Log("%s slot %d: load interrupted respawn",why,k+1);return;}
 int enabled=slots[k].active&&!HeraldBlockedHere(slots[k].npc)?SetRealFlag(SpawnerFlags[k][j],true):0;
 int map=currentMap.load();if(map>=0){const float* anchor=CompanionSpawns[map].spawner[s];char record[512];snprintf(record,sizeof(record),",\"slot\":%d,\"generator\":%d,\"anchor_world\":[%.3f,%.3f,%.3f],\"player_position\":[%.3f,%.3f,%.3f],\"flag_result\":%d,\"known_param_rows\":%d",k+1,9800+s,anchor[0],anchor[1],anchor[2],pp[0],pp[1],pp[2],enabled,(int)scan.local[s].size());Trace("respawn_request",record);}
 Log("%s slot %d (%s): spawner %d of 4, requested player (%.1f, %.1f, %.1f), stable-anchor copies world %d local %d",why,k+1,Roster[slots[k].npc].name,j+1,spot[0],spot[1],spot[2],(int)scan.spawn[s].size(),(int)scan.local[s].size());
}

static void SetActive(int k,bool on) {
 int selected=nextLaunchNpc[k].load();if(selected<0)selected=slots[k].npc;
 if(on&&HeraldBlockedHere(selected)){Log("Summon slot %d blocked: companion Herald is unavailable in Majula",k+1);Trace("herald_majula_block",",\"source\":\"summon\"");return;}
 slots[k].active=on;SaveSlots();
 if(on){if(nextLaunchNpc[k].load()<0){slots[k].autoTeleports=0;TeleportSlot(k,"Summon");}}
 else {slots[k].keepHp=0;slots[k].oldCtrl=0;SlotFlagsOff(k);Log("Dismiss slot %d (%s)",k+1,Roster[slots[k].npc].name);}
}
static void ApplyNpcSelection(int k,int npc) {
 uintptr_t host=PlayerCtrl();int hp=0;if(!host||GameStep()!=30||!ReadI32(host+0x168,hp)||hp<=0||resourceLeases[npc].state.load()!=2)return;
 SetRealFlag(HeraldEnabledFlags[k],false);SetRealFlag(HeraldBusyFlags[k],false);
 
 // An explicit NPC change replaces the old actor; automatic recall never takes this path.
 if(slots[k].active){SlotFlagsOff(k);Sleep(700);if(PlayerCtrl()!=host||GameStep()!=30||!ReadI32(host+0x168,hp)||hp<=0||resourceLeases[npc].state.load()!=2)return;}
 slots[k].ctrl=0;slots[k].hp=slots[k].maxHp=0;slots[k].distance=-1;
 nextLaunchNpc[k]=-1;
 slots[k].keepHp=0;slots[k].oldCtrl=0;slots[k].triedSpawners=0;
 slots[k].npc=npc;SaveSlots();
 int files=PatchRegistFiles(k),mem=PatchRegistMemory(k);
 Log("Slot %d native AI profile: boot %d; %d generator files updated",k+1,ProfileBoot(Roster[npc].enemy),PatchProfileFiles(k));
 Log("Slot %d -> %s (enemy %d, AI %d/%d): %d files, %d loaded tables",k+1,Roster[npc].name,Roster[npc].enemy,Roster[npc].high,Roster[npc].low,files,mem);
 if(slots[k].active){SetRealFlag(HeraldEnabledFlags[k],IsHerald(npc)&&!HeraldBlockedHere(npc));TeleportSlot(k,"Swap");}
}
static void ChooseNpc(int k,int npc) {
 if(k<0||k>=SlotCount||npc<0||npc>=RosterCount)return;
 if(npc==slots[k].npc){nextLaunchNpc[k]=-1;SaveSlots();return;}
 if(!QueueNpcResources(npc)){Log("Slot %d cannot load %s: native resource layout unavailable",k+1,Roster[npc].name);return;}
 nextLaunchNpc[k]=npc;SaveSlots();
 Log("Slot %d loading %s through native character and AI managers",k+1,Roster[npc].name);
}
static void UpdateNpcRequests(){
 for(int k=0;k<SlotCount;k++){
  int npc=nextLaunchNpc[k].load();if(npc<0)continue;
  int state=resourceLeases[npc].state.load();
  if(state==0){QueueNpcResources(npc);continue;}
  if(state==2)ApplyNpcSelection(k,npc);
  else if(state==3){Log("Slot %d could not load %s; existing companion retained",k+1,Roster[npc].name);nextLaunchNpc[k]=-1;SaveSlots();}
 }
 static unsigned loads=0,releases=0,corrections=0,failures=0;
 unsigned a=resourceLoads.load(),b=resourceReleases.load(),c=generatorResourceCorrections.load(),d=resourceFailures.load();
 if(a!=loads||b!=releases||c!=corrections||d!=failures){loads=a;releases=b;corrections=c;failures=d;Log("Native resources: loads %u, balanced releases %u, generator release corrections %u, failures %u, ownership overflow %u",a,b,c,d,generatorResourceOverflow.load());}
}
// Store slot k's other group members: place newly created ones beside the player, recall far ones, apply HP options.
static bool DeferGroupRecall(int k,uintptr_t actor,float distance,const float* pos,ULONGLONG now);
static void UpdateGroupMembers(int k,const uintptr_t* members,int count,const float* pp) {
 Slot& s=slots[k];ULONGLONG now=GetTickCount64();ULONGLONG moveAt[SpawnersPerSlot-1]={};int hp[SpawnersPerSlot-1]={};
 for(int e=0;e<count;e++) {
  int previous=-1;for(int i=0;i<s.extraCount;i++)if(s.extras[i]==members[e])previous=i;
  float pos[3]={},spot[3];int maxHp=0;ReadVec(members[e]+0x90,pos);float d=Distance(pos,pp);GroupSpot(k,e,pp,spot);
  moveAt[e]=previous>=0?s.extraMoveAt[previous]:0;
  // A new member appears at its spawner's map anchor; a known one is recalled only when left far behind.
  if((previous<0&&d>8.0f)||(previous>=0&&d>25.0f&&now>=moveAt[e]&&!DeferGroupRecall(k,members[e],d,pos,now))){if(RequestPosition(members[e],spot))Log("Slot %d group member %d: position request from %.1f m",k+1,e+2,d);moveAt[e]=now+12000;}
  UpdateCompanionHealth(k,members[e],now,hp[e],maxHp);
 }
 if(count!=s.extraCount)Log("Slot %d (%s) group: %d of %d members present",k+1,Roster[s.npc].name,count+(s.ctrl?1:0),GroupCount(k));
 s.extraCount=count;for(int e=0;e<SpawnersPerSlot-1;e++){s.extras[e]=e<count?members[e]:0;s.extraHp[e]=e<count?hp[e]:0;s.extraMoveAt[e]=moveAt[e];}
}
// Match loaded characters to slots by their EnemyParam row (ctrl+0x38 points at the character's EnemyParam row).
static void UpdateSlots(uintptr_t player,const float* pp) {
 uintptr_t gm=0,aim=0,node=0;ReadPtr(image+GameManagerRva,gm);ReadPtr(gm+0x28,aim);ReadPtr(aim+0x10,node);
 uintptr_t found[SlotCount]={},foundNode[SlotCount]={},foundPrev[SlotCount]={};uintptr_t prev=0;
 // Collect every character whose EnemyParam row belongs to an active slot's NPC.
 struct Candidate {uintptr_t ctrl,node,prev;int enemy;float distance;bool used;bool alive;};
 companionHealthSerial++;
 std::vector<Candidate> candidates;
 for(int guard=0;node && guard<1000;guard++) {
  uintptr_t ctrl=0,param=0,next=0;ReadPtr(node+0x38,ctrl);
  if(ctrl && ctrl!=player && ReadPtr(ctrl+0x38,param))
   for(int k=0;k<SlotCount;k++) {
    if(!slots[k].active || !IsEnemyRow(param,Roster[slots[k].npc].enemy))continue;
    float cp[3]={};int hp=0;ReadVec(ctrl+0x90,cp);ReadI32(ctrl+0x168,hp);candidates.push_back({ctrl,node,prev,Roster[slots[k].npc].enemy,Distance(cp,pp),false,hp>0});break;
   }
  if(!ReadPtr(node+0x10,next))break;prev=node;node=next;
 }
 // Each slot takes its current character if still there, otherwise the nearest unused match.
 for(int k=0;k<SlotCount;k++) {
  if(!slots[k].active)continue;
  int best=-1;int matches=0;for(auto& c:candidates)if(c.enemy==Roster[slots[k].npc].enemy)matches++;
  if(matches!=slots[k].matchCount)Log("Candidate count slot %d: %d -> %d (same EnemyParam; may include ordinary summons)",k+1,slots[k].matchCount,matches);slots[k].matchCount=matches;
  // A group keeps a living member as its tracked character, so one death does not trigger a full respawn.
  bool group=GroupCount(k)>1;
  auto better=[&](const Candidate& a,const Candidate& b){if(group&&a.alive!=b.alive)return a.alive;return a.distance<b.distance;};
  for(int i=0;i<(int)candidates.size();i++){auto& c=candidates[i];if(c.used||c.enemy!=Roster[slots[k].npc].enemy)continue;if(c.ctrl==slots[k].ctrl&&(!group||c.alive)){best=i;break;}if(best<0||better(c,candidates[best]))best=i;}
  if(best>=0){candidates[best].used=true;found[k]=candidates[best].ctrl;foundNode[k]=candidates[best].node;foundPrev[k]=candidates[best].prev;}
 }
 // Remaining living matches become the group's other members (previous members first, then nearest).
 uintptr_t extras[SlotCount][SpawnersPerSlot-1]={};int extraCount[SlotCount]={};
 for(int k=0;k<SlotCount;k++) {
  if(!slots[k].active)continue;int want=GroupCount(k)-1;
  for(int pass=0;pass<2;pass++)while(extraCount[k]<want){
   int best=-1;
   for(int i=0;i<(int)candidates.size();i++){auto& c=candidates[i];if(c.used||!c.alive||c.enemy!=Roster[slots[k].npc].enemy)continue;
    bool previous=false;for(int e=0;e<slots[k].extraCount;e++)if(slots[k].extras[e]==c.ctrl)previous=true;
    if(pass==0&&!previous)continue;if(best<0||c.distance<candidates[best].distance)best=i;}
   if(best<0)break;candidates[best].used=true;extras[k][extraCount[k]++]=candidates[best].ctrl;
  }
 }
 AcquireSRWLockExclusive(&slotLock);
 for(int k=0;k<SlotCount;k++) {
  Slot& s=slots[k];
  if(found[k]!=s.ctrl)Log("Actor change slot %d: old 0x%llx -> new 0x%llx, last HP %d",k+1,(unsigned long long)s.ctrl,(unsigned long long)found[k],s.hp);
  if(found[k]!=s.ctrl){if(found[k])Log("Slot %d (%s) present (ctrl 0x%llx)",k+1,Roster[s.npc].name,(unsigned long long)found[k]);else Log("Slot %d (%s) not present",k+1,Roster[s.npc].name);}
  if(s.ctrl && !found[k] && s.hp>0){s.keepHp=s.hp;s.oldCtrl=s.ctrl;}
  if(found[k] && found[k]!=s.ctrl){s.needsPlacement=true;s.placementAttempts=0;s.observedMovement=false;}
  s.ctrl=found[k];
  UpdateGroupMembers(k,extras[k],extraCount[k],pp);
  if(s.ctrl){
   {int h=0,m=0;UpdateCompanionHealth(k,s.ctrl,GetTickCount64(),h,m);}
   int previousHp=s.hp;ReadVec(s.ctrl+0x90,s.pos);ReadI32(s.ctrl+0x168,s.hp);
   if(previousHp>0 && s.hp<=0){s.keepHp=0;Log("Companion death observed slot %d: actor 0x%llx, prior HP %d",k+1,(unsigned long long)s.ctrl,previousHp);}ReadI32(s.ctrl+0x170,s.maxHp);s.distance=Distance(s.pos,pp);if(s.hp>0)s.missingSince=0;else if(!s.missingSince)s.missingSince=GetTickCount64();
   s.missingRetries=0;s.triedSpawners=0;
   if(s.recallAt && GetTickCount64()-s.recallAt>=1500){
    Log("Position result slot %d: same actor %d, distance %.1f m",k+1,s.ctrl==s.recallCtrl,s.distance);s.recallAt=0;if(s.distance>20.0f && s.placementAttempts<2){s.needsPlacement=true;s.nextTeleport=GetTickCount64()+3000;}
   }
   // Streaming can take arbitrarily long. Apply only to a replacement, never the old live actor.
   if(s.keepHp>0 && s.ctrl!=s.oldCtrl && s.hp>0 && s.maxHp>0) {
    int restored=(std::min)(s.keepHp,(std::min)(s.hp,s.maxHp));
    if(WriteI32(s.ctrl+0x168,restored)){Log("Slot %d (%s): HP restored to %d after respawn",k+1,Roster[s.npc].name,restored);s.hp=restored;s.keepHp=0;}
   }
  }
  else {s.distance=-1;if(!s.missingSince)s.missingSince=GetTickCount64();}
  // Combined group HP; the maximum counts every requested member so a lost member shows as missing HP.
  s.groupHp=(s.ctrl&&s.hp>0?s.hp:0);for(int e=0;e<s.extraCount;e++)s.groupHp+=s.extraHp[e];
  s.groupMax=s.ctrl?s.maxHp*(std::max)(GroupCount(k),1+s.extraCount):0;
 }
 ReleaseSRWLockExclusive(&slotLock);
 PruneCompanionHealth();
}

// Only touch an exact tracked actor still owned by the current AI list. A cached
// address and readable HP alone are insufficient during teardown/pointer reuse.
static bool OwnedCompanion(uintptr_t player,const Slot& slot) {
 if(!slot.ctrl || slot.ctrl==player || slot.npc<0 || slot.npc>=RosterCount)return false;
 uintptr_t gm=0,manager=0,node=0,param=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x28,manager)||!manager||!ReadPtr(manager+0x10,node))return false;
 for(int guard=0;node && guard<1024;guard++) {
  uintptr_t ctrl=0,next=0;if(!ReadPtr(node+0x38,ctrl))return false;
  if(ctrl==slot.ctrl)return ReadPtr(ctrl+0x38,param)&&IsEnemyRow(param,Roster[slot.npc].enemy);
  if(!ReadPtr(node+0x10,next)||next==node)return false;node=next;
 }
 return false;
}
static int ReleaseCompanionsOnHostDeath(uintptr_t player) {
 int hostHp=0;
 if(!player || PlayerCtrl()!=player || GameStep()!=30 || !ReadI32(player+0x168,hostHp) || hostHp>0)return 0;
 int requested=0;
 for(int k=0;k<SlotCount;k++) {
  Slot& slot=slots[k];int hp=0;
  if(!OwnedCompanion(player,slot)||!ReadI32(slot.ctrl+0x168,hp)||hp<=0)continue;
  // Use the ordinary KO path while gameplay still updates. Never unlink, free,
  // rescue a destruction-queue entry or call an engine destructor from this thread.
  // Recheck the lifecycle after walking the list; do not write into loading step 21.
  if(PlayerCtrl()!=player || GameStep()!=30)break;
  if(WriteI32(slot.ctrl+0x168,0)) {
   slot.keepHp=hp;slot.oldCtrl=slot.ctrl;slot.hp=0;slot.needsPlacement=false;slot.recallAt=0;requested++;
   char b[256];snprintf(b,sizeof(b),",\"slot\":%d,\"actor\":\"0x%llx\",\"preserved_hp\":%d",k+1,(unsigned long long)slot.ctrl,hp);Trace("host_death_companion_release",b);
  }
  for(int e=0;e<slot.extraCount;e++){Slot member=slot;member.ctrl=slot.extras[e];int memberHp=0;
   if(OwnedCompanion(player,member)&&ReadI32(member.ctrl+0x168,memberHp)&&memberHp>0&&PlayerCtrl()==player&&GameStep()==30&&WriteI32(member.ctrl+0x168,0))requested++;}
  slot.extraCount=0;
 }
 return requested;
}
static void TraceDeathActors(uintptr_t player) {
 int owned=0,alive=0;
 for(const auto& slot:slots)if(OwnedCompanion(player,slot)){owned++;int hp=0;if(ReadI32(slot.ctrl+0x168,hp)&&hp>0)alive++;}
 char b[128];snprintf(b,sizeof(b),",\"tracked_actors_remaining\":%d,\"tracked_actors_alive\":%d",owned,alive);Trace("host_death_cleanup_progress",b);
}
#include "boss-room-recall.h"
#include "native-features.h"
#include "recall-filter.h"
#include "rosabeth-spawn-profile.h"

// Camera research for name/HP labels above heads: log 4x4 matrices one or two pointers from the game manager whose
// translation is within 15 m of the player and whose rotation part is orthonormal (a camera world matrix).
static void FindCameraCandidates(const float* pp) {
 uintptr_t gm=0;if(!ReadPtr(image+GameManagerRva,gm)||!gm)return;
 int logged=0;
 for(int p1=0;p1<0x400 && logged<16;p1+=8) {
  uintptr_t a=0;if(!ReadPtr(gm+p1,a)||!UserPointer(a))continue;
  for(int p2=-8;p2<0x200 && logged<16;p2+=8) {
   uintptr_t b=a;if(p2>=0 && (!ReadPtr(a+p2,b)||!UserPointer(b)))continue;
   for(int o=0;o+64<=0x400 && logged<16;o+=16) {
    float m[16];if(!ReadMatrix(b+o,m))continue;
    float t[3]={m[12],m[13],m[14]};if(!(Distance(t,pp)<15.0f) || fabsf(m[15]-1.0f)>0.001f)continue;
    float r0=m[0]*m[0]+m[1]*m[1]+m[2]*m[2],r1=m[4]*m[4]+m[5]*m[5]+m[6]*m[6],r2=m[8]*m[8]+m[9]*m[9]+m[10]*m[10];
    if(fabsf(r0-1)>0.01f||fabsf(r1-1)>0.01f||fabsf(r2-1)>0.01f)continue;
    if(p2<0)Log("Camera candidate: [gm+0x%x]+0x%x t=(%.2f %.2f %.2f) row2=(%.2f %.2f %.2f)",p1,o,t[0],t[1],t[2],m[8],m[9],m[10]);
    else Log("Camera candidate: [[gm+0x%x]+0x%x]+0x%x t=(%.2f %.2f %.2f) row2=(%.2f %.2f %.2f)",p1,p2,o,t[0],t[1],t[2],m[8],m[9],m[10]);
    logged++;
   }
  }
 }
 Log("Camera search: %d candidates (player at %.2f %.2f %.2f)",logged,pp[0],pp[1],pp[2]);
}

// Keys run on their own thread: the companion thread can be busy for seconds (memory scans, respawns).
static DWORD WINAPI Keys(void*) {
 bool previous[2]={};ULONGLONG nextWatch=0;
 while(true) {
  if(GetTickCount64()>=nextWatch){nextWatch=GetTickCount64()+5000;char state[256];uintptr_t p=PlayerCtrl();int hp=0;if(p)ReadI32(p+0x168,hp);snprintf(state,sizeof(state),",\"host_present\":%s,\"host_hp\":%d,\"game_step\":%d,\"worker_age_ms\":%llu",p?"true":"false",hp,GameStep(),GetTickCount64()-workerHeartbeat.load());Trace("watchdog",state);if(GameStep()==21)TraceLoadingResources();}
  DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
  for(int i=0;i<2;i++) {
   bool down=(GetAsyncKeyState(VK_F7+i)&0x8000)!=0;
   if(down && !previous[i] && foregroundPid==ownPid) {
    if(i==0){menuOpen=!menuOpen.load();Log("Menu %s",menuOpen.load()?"opened":"closed");}
    else for(int k=0;k<SlotCount;k++)if(slots[k].active)Enqueue([k]{slots[k].autoTeleports=0;TeleportSlot(k,"F8 bring");});
   }
   previous[i]=down;
  }
  Sleep(30);
 }
}

// ---- Companion thread: map loads, slot tracking, automatic teleports, queued menu actions ----
static DWORD WINAPI Companion(void*) {
 bool hostDying=false;ULONGLONG nextSnapshot=0,nextDeathTrace=0;
 bool discovering=false;int scanMap=-1;ScanResult pendingScan;ULONGLONG nextScan=0;
 int recallHint=-1,lastPhase=-1;uintptr_t heraldSessionHost=0;
 int lastMap=-1;uintptr_t lastPlayer=0;ULONGLONG playerSince=0,nextUpdate=0;int cameraLogs=0;
 while(true) {
  ULONGLONG now=GetTickCount64();workerHeartbeat=now;
  unsigned corrected=bossGaugeCorrections.exchange(0);if(corrected){char b[100];snprintf(b,sizeof(b),",\"updates\":%u,\"multiplier\":%.1f",corrected,enemyHpMultiplier.load());Trace("boss_gauge_scaled",b);}
  uintptr_t player=PlayerCtrl();
  int map=currentMap.load();
  if(player!=lastPlayer) {
   if(map!=lastMap)for(bool& b:hasMovedSpot)b=false;
   lastMap=map;
   if(player)Log("Player loaded (0x%llx).",(unsigned long long)player);else Log("Loading screen (player 0x%llx gone).",(unsigned long long)lastPlayer);
   if(!registEdits.empty()){Log("Host identity changed with %d unrestored registration edits; dropping stale addresses",(int)registEdits.size());registEdits.clear();}
   scaledEnemies.clear();lastPlayer=player;playerSince=now;scanned=false;discovering=false;nextScan=0;scanCursor=0x10000;scan=ScanResult();enemyTables.clear();for(int k=0;k<SlotCount;k++)slots[k].loadChecked=false;if(!player)for(bool& b:hasMovedSpot)b=false;   // a new map re-creates its spawners
   for(int k=0;k<SlotCount;k++){if(slots[k].ctrl && slots[k].hp>0){slots[k].keepHp=slots[k].hp;slots[k].oldCtrl=slots[k].ctrl;}slots[k].ctrl=0;slots[k].extraCount=0;GroupAreaReset(k);slots[k].nextTeleport=0;slots[k].triedSpawners=0;slots[k].autoTeleports=0;slots[k].missingRetries=0;slots[k].stuckTeleports=0;slots[k].stuckSince=0;slots[k].missingSince=0;if(player)SlotFlagsOff(k);}
   if(!player){flagManager=0;hostDying=false;Log("Host unloaded: cached flag manager discarded; game writes paused");Snapshot("host_unloaded");}
   // All companion flags stay off while the area loads; they are switched on next to the player after the scan
   // below (otherwise the first spawn happens at the spawner's arbitrary default spot).
  }
  int phase=GameStep();
  if(phase!=30)heraldSessionHost=0;
  else if(player&&heraldSessionHost!=player){for(int k=0;k<SlotCount;k++){SetRealFlag(HeraldBusyFlags[k],false);SetRealFlag(HeraldLevelFlags[k],false);}heraldSessionHost=player;}
  if(phase!=lastPhase){
   Log("Game step transition %d -> %d, host 0x%llx",lastPhase,phase,(unsigned long long)player);Snapshot("game_step_changed");
   if(lastPhase==30 && phase==31 && player){
    
    Log("Outgoing step 31: stopping companion generators before host teardown");
    for(int k=0;k<SlotCount;k++){Log("Transition cleanup slot %d begin",k+1);int cleared=SlotFlagsOff(k);Log("Transition cleanup slot %d end: %d of 4 flags cleared",k+1,cleared);}
    Log("Outgoing step 31: restored %d registration rows to their map-load resource IDs",RestoreRegistMemory());
    Snapshot("transition_generators_stopped");
   }
   if(phase==30 && lastPhase!=-1){scanned=false;discovering=false;nextScan=0;scanCursor=0x10000;for(auto& slot:slots){slot.observedMovement=false;slot.nextTeleport=0;}}
   lastPhase=phase;
  }
  if(phase!=30)TickBossRoomRecall(0,now);
  if(phase!=30){Sleep(50);continue;}
  int hostHp=0;bool hostReadable=player && ReadI32(player+0x168,hostHp);
  if(!hostReadable || hostHp<=0) {
   TickBossRoomRecall(0,now);
   if(hostReadable && !hostDying){hostDying=true;Log("Host death detected: pausing companion generation");Snapshot("host_death");
    
    Log("Host death: restored %d registration rows before stopping generators",RestoreRegistMemory());
    for(int k=0;k<SlotCount;k++){Log("Death cleanup slot %d begin",k+1);SlotFlagsOff(k);Log("Death cleanup slot %d end",k+1);}
    int released=ReleaseCompanionsOnHostDeath(player);Log("Host death: requested ordinary KO for %d live tracked companions; saved HP retained",released);nextDeathTrace=0;
   }
   if(hostDying && hostReadable && now>=nextDeathTrace){nextDeathTrace=now+1000;TraceDeathActors(player);}
   Sleep(50);continue;
  }
  if(hostDying){hostDying=false;RestoreRegistMemory();lastPlayer=0;Log("Host recovered: reinitializing companion session");continue;}
  if(now>=nextSnapshot){nextSnapshot=now+2000;Snapshot("status");PlayerOptionsTelemetry();int capacity=0,available=0;if(ReadAnimationPoolUsage(image,capacity,available)){char b[144];snprintf(b,sizeof(b),",\"capacity\":%d,\"available\":%d,\"used\":%d",capacity,available,capacity-available);Trace("animation_resource_pool",b);}}
  if(player && now-playerSince>=1500 && (discovering || !scanned || map!=scanMap || now>=nextScan)) {
   if(!discovering || map!=scanMap){pendingScan=ScanResult();scanCursor=0x10000;scanMap=map;discovering=true;}
   if(ScanMemory(pendingScan,scanMap)) {
   discovering=false;nextScan=GetTickCount64()+60000;scanned=true;scan=std::move(pendingScan);enemyTables.clear();
   if(enemyTables.empty()) {
    for(uintptr_t t:scan.chr)if(ParamRowData(t,810003)&&ParamRowData(t,891500))enemyTables.push_back(t);
    // Invader NPCs (dark phantoms) are on the enemy team (EnemyParam groupType, row offset 0x14). Team 0 makes a
    // character take its summoner's side, like every white summon. Only the loaded table is changed, not files.
    int friendly=0;
    for(uintptr_t t:enemyTables)for(int i=0;i<RosterCount;i++)if(Roster[i].invader){uintptr_t row=ParamRowData(t,(uint32_t)Roster[i].enemy);if(row && WriteI32(row+0x14,0))friendly++;}
    Log("EnemyParam tables: %d; invader NPCs set friendly: %d rows",(int)enemyTables.size(),friendly);
   }
   for(int k=0;k<SlotCount;k++){int m=PatchRegistMemory(k);Log("Slot %d NPC record set to %s in %d loaded tables",k+1,Roster[slots[k].npc].name,m);}
   float pp[3];
   if(ReadVec(player+0x90,pp))for(int k=0;k<SlotCount;k++) {
    if(!slots[k].active || HeraldBlockedHere(slots[k].npc) || slots[k].ctrl || nextLaunchNpc[k].load()>=0)continue;
    SlotFlagsOff(k);
    float spot[3]={pp[0]+1.0f+0.8f*k,pp[1]+0.3f,pp[2]+1.0f-0.8f*k};
    int j=PickSpawner(k,pp),s=SpawnerIndex(k,j);
    slots[k].spawner=j;slots[k].respawnedAt=now;SetRealFlag(SpawnerFlags[k][j],true);
    Log("PARAM discovery slot %d (%s): spawner %d of 4 stable anchor enabled (copies world %d local %d), flag on",k+1,Roster[slots[k].npc].name,j+1,(int)scan.spawn[s].size(),(int)scan.local[s].size());
   }
   if(openFog.load())OpenAreaFogWalls("Area start");
   }
  }
  {std::deque<std::function<void()>> work;{std::lock_guard<std::mutex> g(queueLock);work.swap(queue);}for(auto& w:work)w();}
  TickRosabethSpawnProfile(player,now);
  UpdateNpcRequests();
  if(player && now>=nextUpdate) {
   nextUpdate=now+500;float pp[3];
   if(ReadVec(player+0x90,pp)) {
    UpdateSlots(player,pp);
    UpdateWorldHealth(player);
    UpdateGroupSpawns(pp,now);
    FallGuardTick(pp,now);
    TickBossRoomRecall(player,now);
    for(int k=0;k<SlotCount;k++){bool blocked=HeraldBlockedHere(slots[k].npc);SetRealFlag(HeraldEnabledFlags[k],slots[k].active&&IsHerald(slots[k].npc)&&!blocked);if(blocked)SlotFlagsOff(k);}
    TraceHeraldLevelRequests(player,now);
    if(map!=recallHint){
     Log("Streaming asset hint changed %d -> %d (active map id %d)",recallHint,map,GameMapId());recallHint=map;Snapshot("streaming_hint");
     // An asset hint is telemetry, not evidence that a living actor needs teleporting.
    }
    for(int k=0;k<SlotCount;k++) {
     Slot& s=slots[k];
     if(HeraldBlockedHere(s.npc))continue;
     const char* recall=nullptr;
     if(s.active && s.ctrl && s.hp>0&&!HeraldTalking(k))recall=ObserveRecall(s,pp,now);
     if(recall&&DeferAutomaticRecall(k,recall,pp,now))recall=nullptr;
     if(s.active && s.ctrl && s.hp>0 && s.needsPlacement && now>=s.nextTeleport){
      s.needsPlacement=false;s.placementAttempts++;
      if(s.distance>8.0f){s.nextTeleport=now+12000;TeleportSlot(k,"Spawn placement");continue;}
     }
     if(!s.active || !scanned || now-playerSince<4000 || now<s.nextTeleport || now-s.respawnedAt<4000)continue;
     if(!recall&&TunnelFallbackNeeded(k,pp,now))recall="Auto (tunnel boundary fallback)";
     bool missing=(!s.ctrl || s.hp<=0) && s.missingSince && now-s.missingSince>3000;
     if(recall){s.nextTeleport=now+12000;s.observedMovement=false;TeleportSlot(k,recall);}
     else if(missing&&nextLaunchNpc[k].load()<0){s.missingRetries++;s.nextTeleport=now+10000;TeleportSlot(k,"Auto (missing)");}

    }
   }
  }
  Sleep(50);
 }
}

// ---- Input: hide game keyboard/mouse input from DirectInput while the menu is open ----
using GetDeviceStateFn=HRESULT(WINAPI*)(IDirectInputDevice8W*,DWORD,LPVOID);
using GetDeviceDataFn=HRESULT(WINAPI*)(IDirectInputDevice8W*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
static GetDeviceStateFn originalGetDeviceState;
static GetDeviceDataFn originalGetDeviceData;
static HRESULT WINAPI HookGetDeviceState(IDirectInputDevice8W* d,DWORD size,LPVOID data) {
 HRESULT r=originalGetDeviceState(d,size,data);
 if(SUCCEEDED(r) && menuOpen.load() && data)memset(data,0,size);
 return r;
}
static HRESULT WINAPI HookGetDeviceData(IDirectInputDevice8W* d,DWORD size,LPDIDEVICEOBJECTDATA data,LPDWORD count,DWORD flags) {
 HRESULT r=originalGetDeviceData(d,size,data,count,flags);
 if(SUCCEEDED(r) && menuOpen.load() && count && !(flags&DIGDD_PEEK))*count=0;
 return r;
}
static void HookDirectInput() {
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);wcscat_s(system,L"\\dinput8.dll");
 HMODULE di=LoadLibraryW(system);if(!di){Log("Input hook: system dinput8 missing.");return;}
 auto create=(HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN))GetProcAddress(di,"DirectInput8Create");
 IDirectInput8W* input=nullptr;IDirectInputDevice8W* keyboard=nullptr;
 if(!create || FAILED(create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&input,nullptr)) || FAILED(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr))){Log("Input hook: could not create a DirectInput device.");if(input)input->Release();return;}
 void** vt=*(void***)keyboard;
 bool ok=MH_CreateHook(vt[9],(void*)HookGetDeviceState,(void**)&originalGetDeviceState)==MH_OK && MH_CreateHook(vt[10],(void*)HookGetDeviceData,(void**)&originalGetDeviceData)==MH_OK && MH_EnableHook(vt[9])==MH_OK && MH_EnableHook(vt[10])==MH_OK;
 Log("Input hook: %s",ok?"active":"failed");
 keyboard->Release();input->Release();
}

// ---- Cursor: the game re-centres and clips the cursor every frame; ignore that while the menu is open ----
using SetCursorPosFn=BOOL(WINAPI*)(int,int);using ClipCursorFn=BOOL(WINAPI*)(const RECT*);
static SetCursorPosFn originalSetCursorPos;static ClipCursorFn originalClipCursor;
static BOOL WINAPI HookSetCursorPos(int x,int y){return menuOpen.load()?TRUE:originalSetCursorPos(x,y);}
static BOOL WINAPI HookClipCursor(const RECT* r){return menuOpen.load()?originalClipCursor(nullptr):originalClipCursor(r);}
static void HookCursor() {
 HMODULE user=GetModuleHandleW(L"user32.dll");
 bool ok=MH_CreateHook((void*)GetProcAddress(user,"SetCursorPos"),(void*)HookSetCursorPos,(void**)&originalSetCursorPos)==MH_OK
  && MH_CreateHook((void*)GetProcAddress(user,"ClipCursor"),(void*)HookClipCursor,(void**)&originalClipCursor)==MH_OK
  && MH_EnableHook((void*)GetProcAddress(user,"SetCursorPos"))==MH_OK && MH_EnableHook((void*)GetProcAddress(user,"ClipCursor"))==MH_OK;
 Log("Cursor hook: %s",ok?"active":"failed");
}

// ---- Overlay: Dear ImGui on the game's DirectX 11 swap chain ----
using PresentFn=HRESULT(WINAPI*)(IDXGISwapChain*,UINT,UINT);
using ResizeFn=HRESULT(WINAPI*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
static PresentFn originalPresent;static ResizeFn originalResize;
static ID3D11Device* device;static ID3D11DeviceContext* context;static ID3D11RenderTargetView* target;
static HWND gameWindow;static WNDPROC originalWndProc;static bool overlayReady=false;

static LRESULT CALLBACK OverlayWndProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
 if(overlayReady && menuOpen.load()) {
  ImGui_ImplWin32_WndProcHandler(h,msg,w,l);
  if((msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) || (msg>=WM_KEYFIRST && msg<=WM_KEYLAST && w!=VK_F7) || msg==WM_INPUT)return 0;
 }
 return CallWindowProcW(originalWndProc,h,msg,w,l);
}
static void CreateTarget(IDXGISwapChain* chain) {
 ID3D11Texture2D* back=nullptr;
 if(SUCCEEDED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back))&&back){device->CreateRenderTargetView(back,nullptr,&target);back->Release();}
}
static void HpBar(const char* name,int hp,int maxHp,float width) {
 float f=maxHp>0?(float)hp/maxHp:0.0f;if(f<0)f=0;if(f>1)f=1;
 ImGui::TextUnformatted(name);
 ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(0.75f,0.12f,0.12f,1.0f));
 char label[32];snprintf(label,sizeof(label),"%d / %d",hp,maxHp);
 ImGui::ProgressBar(f,ImVec2(width,0),label);
 ImGui::PopStyleColor();
}
// World -> screen with the camera matrix found by FindCameraCandidates ([gm+0x20]+0x170: translation = camera,
// row 1 = up, row 2 = forward). Forward is flipped if the player would be behind the camera, and right is
// up x forward (left-handed, like Direct3D) so screen left/right cannot come out mirrored.
static float cameraFov=55.0f;
   // 0.10.5 screenshot: labels sat 1.33x too far from centre with 45 (true FOV ~55-58)
static bool WorldToScreen(const float* p,const ImVec2& size,ImVec2& out) {
 uintptr_t gm=0,cam=0,player=PlayerCtrl();float m[16],pp[3];
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x20,cam)||!UserPointer(cam)||!ReadMatrix(cam+0x170,m)||!player||!ReadVec(player+0x90,pp))return false;
 float t[3]={m[12],m[13],m[14]},up[3]={m[4],m[5],m[6]},fw[3]={m[8],m[9],m[10]};
 if(up[1]<0)for(float& v:up)v=-v;
 float toPlayer[3]={pp[0]-t[0],pp[1]-t[1],pp[2]-t[2]};
 if(toPlayer[0]*fw[0]+toPlayer[1]*fw[1]+toPlayer[2]*fw[2]<0)for(float& v:fw)v=-v;
 float rt[3]={up[1]*fw[2]-up[2]*fw[1],up[2]*fw[0]-up[0]*fw[2],up[0]*fw[1]-up[1]*fw[0]};
 float d[3]={p[0]-t[0],p[1]-t[1],p[2]-t[2]};
 float x=d[0]*rt[0]+d[1]*rt[1]+d[2]*rt[2],y=d[0]*up[0]+d[1]*up[1]+d[2]*up[2],z=d[0]*fw[0]+d[1]*fw[1]+d[2]*fw[2];
 if(z<0.3f)return false;
 float f=(size.y*0.5f)/tanf(cameraFov*0.5f*3.14159265f/180.0f);
 out=ImVec2(size.x*0.5f+x/z*f,size.y*0.5f-y/z*f);
 return out.x>-200 && out.x<size.x+200 && out.y>-200 && out.y<size.y+200;
}
// Name and HP bar above each companion's head, like the game's own bars. Labels that would overlap are stacked
// upwards so names stay readable when companions bunch up.
static void HeadLabels(ImDrawList* dl,const Slot* view,const ImVec2& size,float scale) {
 struct Label {ImVec2 at;const Slot* s;};Label labels[SlotCount];int n=0;
 for(int k=0;k<SlotCount;k++) {
  const Slot& s=view[k];if(!s.active || !s.ctrl)continue;
  float pos[3];if(!ReadVec(s.ctrl+0x90,pos))continue;
  float head[3]={pos[0],pos[1]+1.85f,pos[2]};ImVec2 at;if(!WorldToScreen(head,size,at))continue;
  labels[n++]={at,&s};
 }
 float w=110*scale,lineH=ImGui::GetTextLineHeight()+10*scale;
 for(int i=0;i<n;i++)for(int j=0;j<i;j++)
  if(fabsf(labels[i].at.x-labels[j].at.x)<w && fabsf(labels[i].at.y-labels[j].at.y)<lineH)labels[i].at.y=labels[j].at.y-lineH;
 for(int i=0;i<n;i++) {
  const Slot& s=*labels[i].s;ImVec2 at=labels[i].at;
  const char* name=NpcDisplayName(s.npc);ImVec2 ts=ImGui::CalcTextSize(name);
  float h=7*scale,f=s.maxHp>0?(float)s.hp/s.maxHp:0;if(f<0)f=0;if(f>1)f=1;
  dl->AddText(ImVec2(at.x-ts.x*0.5f+1,at.y-ts.y-h-3*scale+1),IM_COL32(0,0,0,200),name);
  dl->AddText(ImVec2(at.x-ts.x*0.5f,at.y-ts.y-h-3*scale),IM_COL32(235,230,215,255),name);
  dl->AddRectFilled(ImVec2(at.x-w*0.5f-1,at.y-h-1),ImVec2(at.x+w*0.5f+1,at.y+1),IM_COL32(0,0,0,170));
  dl->AddRectFilled(ImVec2(at.x-w*0.5f,at.y-h),ImVec2(at.x-w*0.5f+w*f,at.y),IM_COL32(170,25,25,255));
 }
}// ---- 0.44 menu tabs: White summons (summons + story NPCs), Dark summons (invaders), Miscellaneous (the rest) ----
enum class NpcTab {White,Dark,Misc};
static NpcTab TabFor(int i){
 if(strstr(Roster[i].name,"(Invader)"))return NpcTab::Dark;
 if(Roster[i].enemy<990700||!strncmp(Roster[i].from,"Story",5))return NpcTab::White;
 return NpcTab::Misc;
}
static void NpcTable(NpcTab tab,const Slot* view,float scale){
 if(!ImGui::BeginTable("##npcs",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,ImVec2(0,(std::max)(160*scale,ImGui::GetContentRegionAvail().y-4*scale))))return;
 ImGui::TableSetupColumn("Companion",ImGuiTableColumnFlags_WidthStretch,1.0f);ImGui::TableSetupColumn("Area / role",ImGuiTableColumnFlags_WidthStretch,1.2f);ImGui::TableSetupColumn("Use in slot",ImGuiTableColumnFlags_WidthFixed,110*scale);
 ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
 for(int i=0;i<RosterCount;i++){
  if(TabFor(i)!=tab)continue;ImGui::PushID(i);ImGui::TableNextRow();
  ImGui::TableNextColumn();ImGui::TextUnformatted(NpcDisplayName(i));
  ImGui::TableNextColumn();ImGui::TextDisabled("%s",Roster[i].from);
  ImGui::TableNextColumn();
  for(int k=0;k<SlotCount;k++){int current=nextLaunchNpc[k].load();if(current<0)current=view[k].npc;char b[8];snprintf(b,sizeof(b),"%d",k+1);
   if(k)ImGui::SameLine();ImGui::BeginDisabled(current==i||nextLaunchNpc[k].load()>=0);if(ImGui::SmallButton(b))Enqueue([k,i]{ChooseNpc(k,i);});ImGui::EndDisabled();}
  ImGui::PopID();
 }
 ImGui::EndTable();
}
static void DrawOverlay() {
 ImGuiIO& io=ImGui::GetIO();io.MouseDrawCursor=menuOpen.load();
 static bool wasOpen=false;if(menuOpen.load() && !wasOpen){RECT rc;GetClientRect(gameWindow,&rc);POINT c={(rc.right-rc.left)/2,(rc.bottom-rc.top)/2};ClientToScreen(gameWindow,&c);originalSetCursorPos(c.x,c.y);}wasOpen=menuOpen.load();
 float scale=io.DisplaySize.y/1080.0f;if(scale<1)scale=1;io.FontGlobalScale=scale;
 Slot view[SlotCount];AcquireSRWLockShared(&slotLock);for(int k=0;k<SlotCount;k++)view[k]=slots[k];ReleaseSRWLockShared(&slotLock);
 if(!hideCompanionLabels.load())HeadLabels(ImGui::GetBackgroundDrawList(),view,io.DisplaySize,scale);
 // Party panel (top-left, under the player's own bars): name + HP of every companion that is present.
 bool any=false;for(auto& s:view)if(s.active && s.ctrl)any=true;
 if(any) {
  ImGui::SetNextWindowPos(ImVec2(30*scale,190*scale),ImGuiCond_Always);ImGui::SetNextWindowBgAlpha(0.35f);
  ImGui::Begin("##party",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav);
  for(auto& s:view)if(s.active && s.ctrl){
   if(s.extraCount>0){char name[160];snprintf(name,sizeof(name),"%s x%d",NpcDisplayName(s.npc),1+s.extraCount);HpBar(name,s.groupHp,s.groupMax,220*scale);}
   else HpBar(NpcDisplayName(s.npc),s.hp,s.maxHp,220*scale);
  }
  ImGui::End();
 }
 if(!menuOpen.load())return;
 ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.5f),ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
 static bool menuSizeLoaded=false;static ImVec2 menuSize;
 if(!menuSizeLoaded){menuSize=ImVec2((float)GetPrivateProfileIntW(L"overlay",L"menuWidth",1250,ini.c_str())*scale,(float)GetPrivateProfileIntW(L"overlay",L"menuHeight",820,ini.c_str())*scale);menuSizeLoaded=true;}
 ImGui::SetNextWindowSize(menuSize,ImGuiCond_FirstUseEver);ImGui::SetNextWindowSizeConstraints(ImVec2(760*scale,420*scale),ImVec2(100000.0f,100000.0f));
 ImGui::Begin("NPC Companions  (F7 to close)",nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
 // Drag an edge or the corner to resize; the size is kept in settings.ini once the mouse is released.
 {ImVec2 now=ImGui::GetWindowSize();if((now.x!=menuSize.x||now.y!=menuSize.y)&&!ImGui::IsMouseDown(0)){menuSize=now;wchar_t v[16];swprintf_s(v,L"%d",(int)(now.x/scale));WritePrivateProfileStringW(L"overlay",L"menuWidth",v,ini.c_str());swprintf_s(v,L"%d",(int)(now.y/scale));WritePrivateProfileStringW(L"overlay",L"menuHeight",v,ini.c_str());}}
 ImGui::SeparatorText("Party (summon, dismiss, bring)");
 for(int k=0;k<SlotCount;k++) {
  ImGui::PushID(k);
  int selected=nextLaunchNpc[k].load();if(selected<0)selected=view[k].npc;
  ImGui::Text("Slot %d",k+1);ImGui::SameLine(80*scale);ImGui::TextUnformatted(NpcDisplayName(selected));ImGui::SameLine(420*scale);
  bool heraldBlocked=HeraldBlockedHere(selected);
  ImGui::BeginDisabled(!view[k].active&&heraldBlocked);
  if(ImGui::Button(view[k].active?"Dismiss":"Summon",ImVec2(100*scale,0))){bool on=!view[k].active;Enqueue([k,on]{SetActive(k,on);});}
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!view[k].active||HeraldBlockedHere(view[k].npc));
  if(ImGui::Button("Bring to me",ImVec2(120*scale,0)))Enqueue([k]{slots[k].autoTeleports=0;TeleportSlot(k,"Menu bring");});
  ImGui::EndDisabled();
  ImGui::SameLine();
  if(heraldBlocked)ImGui::TextDisabled("Herald unavailable in Majula");
  else if(!view[k].active)ImGui::TextDisabled("dismissed");
  else if(view[k].ctrl && !SlotHasCopies(k) && view[k].distance>25.0f)ImGui::TextDisabled("%.0f m away - rest at a bonfire to bring them to you",view[k].distance);
  else if(view[k].ctrl&&view[k].extraCount>0)ImGui::Text("%d HP total (%d members), %.0f m",view[k].groupHp,1+view[k].extraCount,view[k].distance);
  else if(view[k].ctrl)ImGui::Text("%d HP, %.0f m",view[k].hp,view[k].distance);
  else ImGui::TextDisabled("spawning... (rest at a bonfire if it takes long)");
  ImGui::SameLine();ImGui::SetNextItemWidth(150*scale);
  float hpScale=companionHpScale[k].load();
  if(ImGui::SliderFloat("HP scale",&hpScale,1.0f,99.0f,"%.1fx",ImGuiSliderFlags_AlwaysClamp))companionHpScale[k]=HpMultiplierSetting(hpScale);
  if(ImGui::IsItemDeactivatedAfterEdit())SaveCompanionHealth();
  if(GroupCapable(view[k].npc)){
   bool group=groupEnabled[k].load();int size=groupSize[k].load();
   if(ImGui::Checkbox("Spawn a group",&group)){groupEnabled[k]=group;SaveCompanionHealth();Enqueue([k]{float pp[3];uintptr_t p=PlayerCtrl();if(p&&ReadVec(p+0x90,pp))ApplyGroupFlags(k,pp);});}
   ImGui::SameLine();ImGui::BeginDisabled(!group);ImGui::SetNextItemWidth(150*scale);
   if(ImGui::SliderInt("members",&size,1,MaxGroupSize,"%d",ImGuiSliderFlags_AlwaysClamp))groupSize[k]=size;
   if(ImGui::IsItemDeactivatedAfterEdit()){SaveCompanionHealth();Enqueue([k]{float pp[3];uintptr_t p=PlayerCtrl();if(p&&ReadVec(p+0x90,pp))ApplyGroupFlags(k,pp);});}
   ImGui::EndDisabled();
  }
  if(nextLaunchNpc[k].load()>=0)ImGui::TextDisabled("Loading model and AI... Current: %s",NpcDisplayName(view[k].npc));
  if(heraldBlocked)ImGui::TextDisabled("Use Majula's original Herald. Companion Herald is available after leaving Majula.");
  if(!strcmp(Roster[view[k].npc].name,"Emerald Herald")){
   ImGui::BeginDisabled(!view[k].active||!view[k].ctrl||view[k].hp<=0||view[k].distance<0||view[k].distance>5);
   if(ImGui::Button("Level up with the Herald"))Enqueue([k]{HeraldLevelUp(k);});ImGui::EndDisabled();
   ImGui::TextDisabled("Approach her and use the game's Talk prompt for dialogue and services. F7 level up is also available within 5 m.");
  }
  if(!strcmp(Roster[view[k].npc].name,"Rosabeth of Melfia"))ImGui::TextDisabled("Rosabeth copies gifted clothes at her next native spawn. Rest at a bonfire after gifting. Skin and native combat are retained.");
  ImGui::PopID();
 }
 if(ImGui::BeginTabBar("##tabs")){
  if(ImGui::BeginTabItem("White summons")){NpcTable(NpcTab::White,view,scale);ImGui::EndTabItem();}
  if(ImGui::BeginTabItem("Dark summons")){NpcTable(NpcTab::Dark,view,scale);ImGui::EndTabItem();}
  if(ImGui::BeginTabItem("Miscellaneous")){NpcTable(NpcTab::Misc,view,scale);ImGui::EndTabItem();}
  if(ImGui::BeginTabItem("Options")){ImGui::BeginChild("##options");
 ImGui::Separator();
 ImGui::TextDisabled("NPC selections load automatically. Original quest NPCs remain separate.");
 ImGui::TextDisabled("F8: bring all companions to you. Companions follow normally; recall handles long separation, stationary stragglers and elevator transitions.");
 ImGui::TextDisabled("Spawners use fixed map anchors; newly created companions are brought to you.");
 ImGui::Separator();
 bool hideLabels=hideCompanionLabels.load();
 if(ImGui::Checkbox("Hide floating companion names and HP bars",&hideLabels)){hideCompanionLabels=hideLabels;WritePrivateProfileStringW(L"companions",L"hideFloatingLabels",hideLabels?L"1":L"0",ini.c_str());}
 ImGui::TextDisabled("Hides both labels above summons. The party panel stays visible.");
 ImGui::TextDisabled("Herald rests in her own native idle pose when nearby. Talk and leveling take priority.");
 ImGui::Separator();
 bool hpEnabled=enemyHpEnabled.load();float hpSelected=enemyHpSelected.load();
 if(ImGui::Checkbox("Scale enemy and boss HP",&hpEnabled))SetWorldHpOptions(hpEnabled,hpSelected,true);
 ImGui::BeginDisabled(!hpEnabled);ImGui::SetNextItemWidth(320*scale);
 if(ImGui::SliderFloat("HP multiplier",&hpSelected,1.0f,99.0f,"%.1fx",ImGuiSliderFlags_AlwaysClamp))SetWorldHpOptions(hpEnabled,hpSelected,false);
 if(ImGui::IsItemDeactivatedAfterEdit())SetWorldHpOptions(hpEnabled,hpSelected,true);
 ImGui::EndDisabled();
 ImGui::TextDisabled("1.0x to 99.0x HP. Ctrl+click the slider to enter a number. Damage stays unchanged.");
 ImGui::Separator();
 bool soulsOn=soulsMultiplierEnabled.load();float soulsValue=soulsMultiplierSelected.load();
 if(ImGui::Checkbox("Multiply souls gained",&soulsOn)){soulsMultiplierEnabled=soulsOn;SavePlayerOptions();}
 ImGui::BeginDisabled(!soulsOn);ImGui::SetNextItemWidth(320*scale);
 if(ImGui::SliderFloat("Souls multiplier",&soulsValue,1.0f,99.0f,"%.1fx",ImGuiSliderFlags_AlwaysClamp))soulsMultiplierSelected=HpMultiplierSetting(soulsValue);
 if(ImGui::IsItemDeactivatedAfterEdit())SavePlayerOptions();ImGui::EndDisabled();
 ImGui::TextDisabled("Multiplies increases to carried souls. Soul Memory uses the normal gain.");
 bool durability=infiniteDurability.load();if(ImGui::Checkbox("Infinite equipment durability",&durability)){infiniteDurability=durability;SavePlayerOptions();}
 ImGui::Separator();
 bool fog=openFog.load();
 if(ImGui::Checkbox("Open area fog walls (not boss doors)",&fog)){openFog=fog;WritePrivateProfileStringW(L"world",L"openFog",fog?L"1":L"0",ini.c_str());if(fog)Enqueue([]{OpenAreaFogWalls("Menu");});}
 ImGui::TextDisabled("Opened fog walls stay open in this save (.snc) even if you turn this off later.");
 ImGui::Separator();
 bool bossRecall=autoBossRecall.load();
 if(ImGui::Checkbox("Bring companions into boss rooms automatically",&bossRecall)){autoBossRecall=bossRecall;WritePrivateProfileStringW(L"companions",L"autoBossRecall",bossRecall?L"1":L"0",ini.c_str());}
 ImGui::TextDisabled("Recalls once after the native boss health bar appears and entry animations finish. F8 is the manual fallback.");
 bool careful=conservativeRecall.load();
 if(ImGui::Checkbox("Reduce unnecessary automatic teleports",&careful)){conservativeRecall=careful;WritePrivateProfileStringW(L"companions",L"conservativeRecall",careful?L"1":L"0",ini.c_str());}
 ImGui::TextDisabled("Lets fighting companions hold position and gives nearby followers more time. Emergency recovery stays active.");
 bool tunnel=tunnelRecovery.load();
 if(ImGui::Checkbox("Recover companions stuck at tunnel boundaries",&tunnel)){tunnelRecovery=tunnel;WritePrivateProfileStringW(L"companions",L"tunnelRecovery",tunnel?L"1":L"0",ini.c_str());}
 bool guard=fallGuard.load();
 if(ImGui::Checkbox("Fall guard for companion teleports (test)",&guard)){fallGuard=guard;WritePrivateProfileStringW(L"companions",L"fallGuard",guard?L"1":L"0",ini.c_str());}
 ImGui::TextDisabled("Holds companion moves while area data loads; brings back a companion that drops after a move.");
 bool regen=companionRegen.load();
 if(ImGui::Checkbox("Passive HP regeneration for companions",&regen)){companionRegen=regen;SaveCompanionHealth();}
 ImGui::TextDisabled("Companions recover %d HP per second while alive. HP scale sliders set each slot's max HP (1.0x-99.0x).",CompanionRegenPerSecond);
   ImGui::EndChild();ImGui::EndTabItem();}
  ImGui::EndTabBar();
 }
 ImGui::End();
}
static HRESULT WINAPI HookPresent(IDXGISwapChain* chain,UINT sync,UINT flags) {
 if(!overlayReady) {
  static bool tried=false;
  if(!tried) {
   tried=true;
   DXGI_SWAP_CHAIN_DESC desc;
   if(SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device),(void**)&device)) && SUCCEEDED(chain->GetDesc(&desc))) {
    device->GetImmediateContext(&context);gameWindow=desc.OutputWindow;
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;ImGui::StyleColorsDark();
    if(ImGui_ImplWin32_Init(gameWindow) && ImGui_ImplDX11_Init(device,context)) {
     originalWndProc=(WNDPROC)SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,(LONG_PTR)OverlayWndProc);
     CreateTarget(chain);overlayReady=true;Log("Overlay ready (%ux%u).",desc.BufferDesc.Width,desc.BufferDesc.Height);
    } else Log("Overlay: ImGui backend init failed.");
   } else Log("Overlay: swap chain is not Direct3D 11.");
  }
 }
 if(overlayReady) {
  if(!target)CreateTarget(chain);
  ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
  DrawOverlay();
  ImGui::Render();
  if(target){context->OMSetRenderTargets(1,&target,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());}
 }
 return originalPresent(chain,sync,flags);
}
static HRESULT WINAPI HookResize(IDXGISwapChain* chain,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags) {
 if(target){target->Release();target=nullptr;}
 return originalResize(chain,count,w,h,format,flags);
}
static void HookSwapChain() {
 // A throwaway device + swap chain on a hidden window gives the IDXGISwapChain vtable shared with the game's.
 WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=selfModule;wc.lpszClassName=L"NPCCompanionsDummy";RegisterClassExW(&wc);
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,selfModule,nullptr);
 DXGI_SWAP_CHAIN_DESC d={};d.BufferCount=1;d.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.OutputWindow=window;d.SampleDesc.Count=1;d.Windowed=TRUE;
 IDXGISwapChain* chain=nullptr;ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;
 HRESULT r=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&chain,&dev,nullptr,&ctx);
 if(FAILED(r))r=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&chain,&dev,nullptr,&ctx);
 if(SUCCEEDED(r)) {
  void** vt=*(void***)chain;
  bool ok=MH_CreateHook(vt[8],(void*)HookPresent,(void**)&originalPresent)==MH_OK && MH_CreateHook(vt[13],(void*)HookResize,(void**)&originalResize)==MH_OK && MH_EnableHook(vt[8])==MH_OK && MH_EnableHook(vt[13])==MH_OK;
  Log("Overlay hook: %s",ok?"active":"failed");
  chain->Release();dev->Release();ctx->Release();
 } else Log("Overlay hook: could not create a Direct3D 11 device (0x%08x).",(unsigned)r);
 DestroyWindow(window);UnregisterClassW(wc.lpszClassName,selfModule);
}
static DWORD WINAPI HookLater(void*) {Sleep(3000);HookSwapChain();HookDirectInput();HookCursor();return 0;}

// The mod plays on a separate save (.snc). Copy each normal .sl2 once (with a backup); never overwrite either.
static void CopySavesOnce() {
 wchar_t appdata[MAX_PATH];if(!GetEnvironmentVariableW(L"APPDATA",appdata,MAX_PATH))return;
 std::wstring saves=std::wstring(appdata)+L"\\DarkSoulsII";WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW((saves+L"\\*").c_str(),&d);if(h==INVALID_HANDLE_VALUE)return;
 do {
  if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||d.cFileName[0]==L'.')continue;
  std::wstring dir=saves+L"\\"+d.cFileName;WIN32_FIND_DATAW s;HANDLE hs=FindFirstFileW((dir+L"\\*.sl2").c_str(),&s);if(hs==INVALID_HANDLE_VALUE)continue;
  do {
   std::wstring src=dir+L"\\"+s.cFileName,dst=src.substr(0,src.size()-4)+L".snc";
   if(GetFileAttributesW(dst.c_str())!=INVALID_FILE_ATTRIBUTES)continue;
   std::wstring backupDir=root+L"\\save-backups\\"+d.cFileName;CreateDirectoryW((root+L"\\save-backups").c_str(),nullptr);CreateDirectoryW(backupDir.c_str(),nullptr);
   CopyFileW(src.c_str(),(backupDir+L"\\"+s.cFileName).c_str(),TRUE);
   Log("Companion save %ls: %s",dst.c_str(),CopyFileW(src.c_str(),dst.c_str(),TRUE)?"created from normal save":"copy FAILED");
  } while(FindNextFileW(hs,&s));
  FindClose(hs);
 } while(FindNextFileW(h,&d));
 FindClose(h);
}

static bool pluginMode=false;
static INIT_ONCE initializeOnce=INIT_ONCE_STATIC_INIT;static DWORD initializeResult=0;
static DWORD InitializeOnce();
static BOOL CALLBACK RunInitialize(PINIT_ONCE,void*,void**){initializeResult=InitializeOnce();return TRUE;}
extern "C" __declspec(dllexport) DWORD WINAPI Initialize(void*) {InitOnceExecuteOnce(&initializeOnce,RunInitialize,nullptr,nullptr);return initializeResult;}
static DWORD InitializeOnce() {
 wchar_t modulePath[MAX_PATH];if(!GetModuleFileNameW(selfModule,modulePath,MAX_PATH))return 1;
 wchar_t* slash=wcsrchr(modulePath,L'\\');if(!slash)return 2;*slash=0;root=modulePath;
 // Loaded from Game\dllMods (Lazy Loader): the data folder is Game\\NPCCompanions.
 if(GetFileAttributesW((root+L"\\assets").c_str())==INVALID_FILE_ATTRIBUTES){wchar_t exe[MAX_PATH];if(GetModuleFileNameW(nullptr,exe,MAX_PATH)){wchar_t* s2=wcsrchr(exe,L'\\');if(s2)*s2=0;root=std::wstring(exe)+L"\\NPCCompanions";}}
 assets=root+L"\\assets";ini=root+L"\\settings.ini";logPath=root+L"\\NPCCompanions.log";
 image=(uintptr_t)GetModuleHandleW(nullptr);ownPid=GetCurrentProcessId();
 SYSTEMTIME journalTime;GetSystemTime(&journalTime);wchar_t journalName[160];swprintf_s(journalName,L"\\logs\\session-%04u%02u%02u-%02u%02u%02u-%lu.jsonl",journalTime.wYear,journalTime.wMonth,journalTime.wDay,journalTime.wHour,journalTime.wMinute,journalTime.wSecond,ownPid);
 CreateDirectoryW((root+L"\\logs").c_str(),nullptr);tracePath=root+journalName;
 Trace("session_start",",\"version\":\"0.44\",\"schema\":1,\"exe_sha256_expected\":\"0045931b8914504531b7864a9488d396dc50cbaf524964016e1d69c3d1173131\"");
 Log("==== NPC Companions 0.44 test build (tabbed menu, Lazy Loader support; offensive HP gates removed, native boss phase/curse dispatch; automatic boss-room entry recall; 100 m panoramic/vertical perception, conservative recall filter; combat target retention, native Herald idle, gesture mirroring removed; repeat boss attacks, boss-fog checks removed, optional floating labels; neutral Herald; white-phantom category for mob/boss companions; fall guard; companion AI: boss-fog checks removed, boss-mode redirects suppressed, action completion guards; 5 Sunken King/Pursuer companions; companion HP regen, HP scale, mob groups of up to 6, staggered group spawns; source-native combat profiles; native targeting; host lock guard; local following, native mobs, animation pool repair) ====");
 if(GetModuleHandleW(L"NPCSpawner.dll")){Log("NPC Spawner with Progression is also loaded. Disable one of the two mods; refusing startup.");return 13;}
 if(pluginMode)Log("Loaded as a plugin (dllMods / Lazy Loader).");
 {bool expanded=false;if(!AnimationPoolPatchState(image,expanded)||!expanded){if(pluginMode)Log("WARNING: animation pool expansion unavailable (the loader started this mod late). Large areas such as No-man's Wharf may crash; use the bundled loader if they do.");else{Log("Animation registry capacity patch missing; refusing companion startup.");return 11;}}int capacity=0,available=0;if(ReadAnimationPoolUsage(image,capacity,available)&&capacity!=(int)AnimationPoolCapacity){Log("Animation registry initialized too early with capacity %d; refusing companion startup.",capacity);return 12;}Log("Animation resource registry: constructor capacity %u, allocation %u bytes; all three operands verified.",AnimationPoolCapacity,AnimationPoolBytes);}
 for(const auto& s:Signatures)if(memcmp((void*)(image+s.rva),s.bytes,s.size)!=0){Log("Unsupported executable or conflicting patch. Refusing startup.");return 4;}
 const uint8_t offline[]={0xe9,0xf0,0,0,0,0x90};const wchar_t extension[]=L"snc";
 if(!Patch(image+OfflineRva,offline,sizeof(offline)) || !Patch(image+SaveRva+2,extension,sizeof(extension))){Log("Protection patches failed.");return 5;}
 CopySavesOnce();
 openFog=GetPrivateProfileIntW(L"world",L"openFog",0,ini.c_str())!=0;
 autoBossRecall=GetPrivateProfileIntW(L"companions",L"autoBossRecall",1,ini.c_str())!=0;
 LoadWorldHpOptions();
 LoadPlayerOptions();
 LoadCompanionHealth();
 hideCompanionLabels=GetPrivateProfileIntW(L"companions",L"hideFloatingLabels",1,ini.c_str())!=0;
 tunnelRecovery=GetPrivateProfileIntW(L"companions",L"tunnelRecovery",1,ini.c_str())!=0;
 conservativeRecall=GetPrivateProfileIntW(L"companions",L"conservativeRecall",1,ini.c_str())!=0;
 fallGuard=GetPrivateProfileIntW(L"companions",L"fallGuard",1,ini.c_str())!=0;
 LoadSlots();cameraFov=(float)GetPrivateProfileIntW(L"overlay",L"fov",55,ini.c_str());
 for(int k=0;k<SlotCount;k++){startupHigh[k]=Roster[slots[k].npc].high;startupLow[k]=Roster[slots[k].npc].low;}
 for(int k=0;k<SlotCount;k++)Log("Slot %d: %s, %s, NPC record files patched: %d",k+1,Roster[slots[k].npc].name,slots[k].active?"active":"dismissed",PatchRegistFiles(k));
 for(int k=0;k<SlotCount;k++)Log("Slot %d native AI profile boot %d: %d generator files updated",k+1,ProfileBoot(Roster[slots[k].npc].enemy),PatchProfileFiles(k));
 grow=(GrowFn)(image+GrowRva);
 resourceApi={(decltype(resourceApi.characterLoad))(image+CharacterLoadRva),(decltype(resourceApi.characterRelease))(image+CharacterReleaseRva),(decltype(resourceApi.characterReady))(image+CharacterReadyRva),(decltype(resourceApi.aiLoad))(image+AiLoadRva),(decltype(resourceApi.aiRelease))(image+AiReleaseRva),(decltype(resourceApi.aiReady))(image+AiReadyRva)};
 // 0.15: the keep-loaded engine patch (removal-queue hook at exe+0x35b9c0) is removed; it caused frequent crashes.
 if(MH_Initialize()!=MH_OK || MH_CreateHook((void*)(image+PathRva),(void*)Redirect,(void**)&originalPath)!=MH_OK || MH_CreateHook((void*)(image+FlagRva),(void*)GetFlag,(void**)&originalFlag)!=MH_OK||MH_CreateHook((void*)(image+BossGaugeSetupRva),(void*)HookBossGaugeSetup,(void**)&originalBossGaugeSetup)!=MH_OK||MH_CreateHook((void*)(image+BossGaugeUpdateRva),(void*)HookBossGaugeUpdate,(void**)&originalBossGaugeUpdate)!=MH_OK||MH_CreateHook((void*)(image+GeneratorResourceTickRva),(void*)HookGeneratorResourceTick,(void**)&originalGeneratorResourceTick)!=MH_OK||MH_CreateHook((void*)(image+CharacterUnloadRva),(void*)HookCharacterUnload,(void**)&originalCharacterUnload)!=MH_OK||MH_CreateHook((void*)(image+GeneratorLoadRva),(void*)HookGeneratorLoad,(void**)&originalGeneratorLoad)!=MH_OK||MH_CreateHook((void*)(image+GeneratorReleaseRva),(void*)HookGeneratorRelease,(void**)&originalGeneratorRelease)!=MH_OK||MH_CreateHook((void*)(image+GiveSoulsRva),(void*)HookGiveSouls,(void**)&originalGiveSouls)!=MH_OK||MH_CreateHook((void*)(image+DurabilityDamageRva),(void*)HookDurabilityDamage,(void**)&originalDurabilityDamage)!=MH_OK||MH_CreateHook((void*)(image+NpcVoiceRequestRva),(void*)HookNpcVoiceRequest,(void**)&originalNpcVoiceRequest)!=MH_OK||MH_CreateHook((void*)(image+NpcVoicePlayingRva),(void*)HookNpcVoicePlaying,(void**)&originalNpcVoicePlaying)!=MH_OK||MH_CreateHook((void*)(image+NpcVoiceStopRva),(void*)HookNpcVoiceStop,(void**)&originalNpcVoiceStop)!=MH_OK||MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();Log("Hook activation failed; hooks removed.");return 8;}
 Log("Rosabeth private status/skin 990718: donation globals update only the constructor armor profile for her next safe spawn; live equipment/model refresh disabled.");
 heraldMenuLayoutReady=true;resourceLayoutReady=true;
 HANDLE t=CreateThread(nullptr,0,Companion,nullptr,0,nullptr);if(!t)return 9;CloseHandle(t);
 t=CreateThread(nullptr,0,Keys,nullptr,0,nullptr);if(!t)return 9;CloseHandle(t);
 t=CreateThread(nullptr,0,HookLater,nullptr,0,nullptr);if(t)CloseHandle(t);
 Log("Native combat profiles: source attack/spell trees retained; native targeting restored except host lock-on guard. Combat target retained up to 100 m while alive and visible or attacking; host follow scan leash 35 m. Native attacks/spells retained; companion boss-fog checks removed.");
 Log("Initialization complete: %d NPCs available. F7 menu, F8 bring all.",RosterCount);return 0;
}
static DWORD WINAPI PluginStart(void*){DWORD r=Initialize(nullptr);if(r)Log("Plugin startup refused (%lu).",r);return r;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,void*) {
 if(why==DLL_PROCESS_ATTACH){selfModule=h;DisableThreadLibraryCalls(h);
  // The bundled dinput8 proxy sets this variable and calls Initialize itself. Any other loader (Lazy Loader dllMods,
  // chainloaders) gets the early animation-pool patch here and a startup thread.
  wchar_t flag[4];if(!GetEnvironmentVariableW(L"NPCCOMPANIONS_BOOTSTRAP",flag,4)){pluginMode=true;ExpandAnimationPoolBeforeStartup((uintptr_t)GetModuleHandleW(nullptr));HANDLE t=CreateThread(nullptr,0,PluginStart,nullptr,0,nullptr);if(t)CloseHandle(t);}
 }
 return TRUE;
}


