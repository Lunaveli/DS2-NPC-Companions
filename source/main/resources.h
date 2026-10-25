#pragma once
// Native generator +0x412970 pairs these exact character/AI APIs with
// +0x412df0. Only engine update/teardown callbacks call them, never the worker.
struct ResourceApi {
 void (*characterLoad)(uintptr_t,int,unsigned char)=nullptr;
 void (*characterRelease)(uintptr_t,int)=nullptr;
 bool (*characterReady)(uintptr_t,int)=nullptr;
 void (*aiLoad)(uintptr_t,int)=nullptr;
 bool (*aiRelease)(uintptr_t,int)=nullptr;
 bool (*aiReady)(uintptr_t,int)=nullptr;
};
static ResourceApi resourceApi;
struct ResourceLease {std::atomic<int> state{0};uintptr_t characters=0,ai=0;ULONGLONG began=0;};
// 0 unused, 1 requested, 2 ready, 3 failed, 4 native load in progress.
static ResourceLease resourceLeases[RosterCount];
static std::atomic<unsigned> resourceLoads{0},resourceReleases{0},resourceFailures{0};
static bool resourceLayoutReady=false;
static bool QueueNpcResources(int npc){
 if(!resourceLayoutReady||npc<0||npc>=RosterCount)return false;
 int expected=0;resourceLeases[npc].state.compare_exchange_strong(expected,1);return true;
}
static void NativeResourceTick(uintptr_t ai){
 if(!resourceLayoutReady||GameStep()!=30)return;
 uintptr_t gm=0,actualAi=0,characters=0;int hp=0;uintptr_t player=PlayerCtrl();
 if(!player||!ReadI32(player+0x168,hp)||hp<=0||!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0x28,actualAi)||ai!=actualAi||!ReadPtr(gm+0x18,characters)||!characters)return;
 ULONGLONG now=GetTickCount64();bool started=false;
 for(int i=0;i<RosterCount;i++){
  auto& lease=resourceLeases[i];int state=lease.state.load();const auto& npc=Roster[i];
  if(state==1&&!started){
   started=true;lease.characters=characters;lease.ai=ai;lease.began=now;
   resourceApi.characterLoad(characters,npc.enemy,0);
   resourceApi.aiLoad(ai,npc.low);resourceApi.aiLoad(ai,npc.high);
   lease.state=4;resourceLoads++;
  }
  if(lease.state==4){
   if(lease.characters!=characters||lease.ai!=ai){lease.state=3;resourceFailures++;continue;}
   if(resourceApi.characterReady(characters,npc.enemy)&&resourceApi.aiReady(ai,npc.low)&&resourceApi.aiReady(ai,npc.high))lease.state=2;
   else if(now-lease.began>30000){lease.state=3;resourceFailures++;}
  }
 }
}
static void ReleaseResourceLeases(uintptr_t characters){
 uintptr_t gm=0,ai=0;ReadPtr(image+GameManagerRva,gm);if(gm)ReadPtr(gm+0x28,ai);
 for(int i=0;i<RosterCount;i++){
  auto& lease=resourceLeases[i];int state=lease.state.load();
  if(lease.characters==characters&&(state==2||state==3||state==4)){
   if(lease.ai==ai&&ai){resourceApi.aiRelease(ai,Roster[i].low);resourceApi.aiRelease(ai,Roster[i].high);}
   resourceApi.characterRelease(characters,Roster[i].enemy);resourceReleases++;
  }
  lease.characters=lease.ai=0;lease.began=0;lease.state=0;
 }
}
using GeneratorResourceTickFn=void(*)(uintptr_t,uintptr_t);
static GeneratorResourceTickFn originalGeneratorResourceTick;
static void HookGeneratorResourceTick(uintptr_t manager,uintptr_t frame){
 uintptr_t gm=0,ai=0,actualManager=0;
 if(ReadPtr(image+GameManagerRva,gm)&&gm&&ReadPtr(gm+0x40,actualManager)&&manager==actualManager&&ReadPtr(gm+0x28,ai)&&ai)NativeResourceTick(ai);
 originalGeneratorResourceTick(manager,frame);
}
using CharacterUnloadFn=void(*)(uintptr_t);
static CharacterUnloadFn originalCharacterUnload;
static void HookCharacterUnload(uintptr_t characters){
 // Native +0x359620 removes the actors before draining its resources. Keep our
 // extra leases until that teardown has finished, then balance each load once.
 originalCharacterUnload(characters);ReleaseResourceLeases(characters);
}

struct GeneratorResourceOwner {uintptr_t generator=0;int enemy=0,high=0,low=0;};
static GeneratorResourceOwner generatorResourceOwners[256]; // native game thread
static std::atomic<unsigned> generatorResourceCorrections{0},generatorResourceOverflow{0};
using GeneratorLoadFn=void(*)(uintptr_t,unsigned char);
using GeneratorReleaseFn=void(*)(uintptr_t);
static GeneratorLoadFn originalGeneratorLoad;
static GeneratorReleaseFn originalGeneratorRelease;
static bool GeneratorIds(uintptr_t generator,int* ids,int& bits){
 uintptr_t row=0;int packed=0;
 if(!ReadPtr(generator+0x60,row)||!row||!ReadI32(generator+0x8c,packed)||!ReadIds(row,ids))return false;
 bits=(packed>>16)&255;return true;
}
static void HookGeneratorLoad(uintptr_t generator,unsigned char priority){
 int ids[3],bits=0;bool capture=GeneratorIds(generator,ids,bits)&&!(bits&2),ours=false;
 if(capture)for(const auto& npc:Roster)if(npc.enemy==ids[0]){ours=true;break;}
 originalGeneratorLoad(generator,priority);
 if(!ours||!GeneratorIds(generator,ids,bits)||!(bits&2))return;
 // Capture what the native loader acquired. A later registration edit must
 // never change the IDs used to release this particular generator's leases.
 for(auto& owner:generatorResourceOwners)if(owner.generator==generator){owner={generator,ids[0],ids[1],ids[2]};return;}
 for(auto& owner:generatorResourceOwners)if(!owner.generator){owner={generator,ids[0],ids[1],ids[2]};return;}
 generatorResourceOverflow++;
}
static void HookGeneratorRelease(uintptr_t generator){
 GeneratorResourceOwner* owner=nullptr;for(auto& entry:generatorResourceOwners)if(entry.generator==generator){owner=&entry;break;}
 int ids[3],bits=0;
 if(!owner||!GeneratorIds(generator,ids,bits)||!(bits&2)){if(owner)*owner={};originalGeneratorRelease(generator);return;}
 if(ids[0]==owner->enemy&&ids[1]==owner->high&&ids[2]==owner->low){*owner={};originalGeneratorRelease(generator);return;}
 uintptr_t gm=0,ai=0,characters=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm){originalGeneratorRelease(generator);return;}
 ReadPtr(gm+0x28,ai);ReadPtr(gm+0x18,characters);
 // Exact native +0x412df0 behavior, using the acquired IDs. No actor removal,
 // pointer substitution, direct reference-count edits or loading-wait bypass.
 if(ai){if(owner->low>=0)resourceApi.aiRelease(ai,owner->low);if(owner->high>=0)resourceApi.aiRelease(ai,owner->high);}
 if(characters)resourceApi.characterRelease(characters,owner->enemy);
 __try{_InterlockedAnd8((char*)(generator+0x8e),(char)~2);}__except(EXCEPTION_EXECUTE_HANDLER){}
 *owner={};generatorResourceCorrections++;
}
