#pragma once
// All entry points and layouts below are pinned to version.h's executable signatures.
using NpcInputFn=void(*)(uintptr_t);
static NpcInputFn originalNpcInput;
static void HookNpcInput(uintptr_t node){originalNpcInput(node);AfterNativeNpcInput(node);}

// The companion's private talk/text IDs are not native FMOD events. Route
// only its private voice IDs to the existing Majula Herald events. The audio
// library loads their original sample bank on demand; actor/source position,
// native timing and cancellation remain under the event manager's control.
static bool ResolveHeraldVoice(int& map,int& voice){
 if(map<0||voice<99080000||voice>=99180000)return false;
 voice=70000000+(voice-99080000);map=0x0a040000;return true;
}
using NpcVoiceRequestFn=float(*)(uintptr_t,const uint32_t*,int,int);
using NpcVoicePlayingFn=bool(*)(uintptr_t,int,int);
using NpcVoiceStopFn=void(*)(uintptr_t,int,int);
static NpcVoiceRequestFn originalNpcVoiceRequest;
static NpcVoicePlayingFn originalNpcVoicePlaying;
static NpcVoiceStopFn originalNpcVoiceStop;
static float HookNpcVoiceRequest(uintptr_t manager,const uint32_t* actor,int map,int voice){
 int sourceMap=map,sourceVoice=voice;bool routed=ResolveHeraldVoice(map,voice);
 float duration=originalNpcVoiceRequest(manager,actor,map,voice);
 if(routed){char data[224];snprintf(data,sizeof(data),",\"source_map\":%d,\"private_voice\":%d,\"native_map\":%d,\"native_voice\":%d,\"duration\":%.3f",sourceMap,sourceVoice,map,voice,JournalNumber(duration));Trace("herald_voice_request",data);}
 return duration;
}
static bool HookNpcVoicePlaying(uintptr_t manager,int map,int voice){ResolveHeraldVoice(map,voice);return originalNpcVoicePlaying(manager,map,voice);}
static void HookNpcVoiceStop(uintptr_t manager,int map,int voice){ResolveHeraldVoice(map,voice);originalNpcVoiceStop(manager,map,voice);}

struct HeraldLevelObservation {uintptr_t actor=0;ULONGLONG began=0;bool requested=false;};
static HeraldLevelObservation heraldLevelObservations[SlotCount];
static void TraceHeraldLevelRequests(uintptr_t player,ULONGLONG now){
 uintptr_t manager=flagManager.load();if(!manager||!originalFlag)return;
 for(int k=0;k<SlotCount;k++){
  auto& previous=heraldLevelObservations[k];const auto& slot=slots[k];
  bool valid=slot.active&&IsHerald(slot.npc)&&OwnedCompanion(player,slot);
  if(!valid){previous=HeraldLevelObservation();continue;}
  if(previous.actor!=slot.ctrl)previous={slot.ctrl,0,false};
  bool requested=originalFlag(manager,HeraldLevelFlags[k])!=0;
  if(requested!=previous.requested){
   uintptr_t ai=CompanionAi(player,slot);int shared=-1,packed=0;if(ai&&ReadI32(ai+0x3a7,packed))shared=packed&255;
   char text[224];snprintf(text,sizeof(text),",\"slot\":%d,\"requested\":%s,\"request_flag\":%u,\"elapsed_ms\":%llu,\"animation_completion\":%d",k+1,requested?"true":"false",HeraldLevelFlags[k],(unsigned long long)(requested?0:now-previous.began),shared);
   Trace("herald_level_up",text);previous.began=requested?now:0;previous.requested=requested;
  }
 }
}

struct BossGaugeScale {uintptr_t scene=0;int base=0,applied=0;};
static BossGaugeScale bossGaugeScales[8]; // game UI thread only; no retained actor pointers
static std::atomic<unsigned> bossGaugeCorrections{0};
using BossGaugeSetupFn=void(*)(uintptr_t,int,uint16_t,int,bool,int);
using BossGaugeUpdateFn=void(*)(uintptr_t,float);
static BossGaugeSetupFn originalBossGaugeSetup;
static BossGaugeUpdateFn originalBossGaugeUpdate;
static void ForgetBossGauge(uintptr_t scene){for(auto& g:bossGaugeScales)if(g.scene==scene)g=BossGaugeScale();}
static void HookBossGaugeSetup(uintptr_t scene,int name,uint16_t entity,int maximum,bool damage,int type){
 ForgetBossGauge(scene);originalBossGaugeSetup(scene,name,entity,maximum,damage,type);
}
static bool CorrectBossGauge(uintptr_t scene,float mode){
 if(!ValidHpMultiplier(mode))return false;
 __try {
  if(*(uintptr_t*)scene!=image+0x10b0c68)return false;
  // Type 0 refreshes max/current directly from the actor at +0x60df0. Group
  // gauges cache their original maximum at +0xd8 and receive live HP at +0xdc.
  if(*(int*)(scene+0xd0)==0){ForgetBossGauge(scene);return false;}
  int maximum=*(int*)(scene+0xd8);if(maximum<=0)return false;
  BossGaugeScale* record=nullptr;for(auto& g:bossGaugeScales)if(g.scene==scene){record=&g;break;}
  if(!record){for(auto& g:bossGaugeScales)if(!g.scene){record=&g;break;}if(!record){static unsigned replace=0;record=&bossGaugeScales[(replace++)%8];}*record={scene,maximum,maximum};}
  if(maximum!=record->applied){record->base=maximum;record->applied=maximum;}
  double desired=round((double)record->base*mode);if(desired<=0||desired>INT_MAX)return false;int target=(int)desired;if(target==maximum)return false;
  *(int*)(scene+0xd8)=(int)target;record->applied=(int)target;bossGaugeCorrections++;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void HookBossGaugeUpdate(uintptr_t scene,float dt){
 CorrectBossGauge(scene,enemyHpMultiplier.load());originalBossGaugeUpdate(scene,dt);
}

static bool heraldMenuLayoutReady=false;
static ULONGLONG lastHeraldMenu=0;
static bool CallAttributeMenu(uintptr_t windows,const float* position){
 using AttribMenuFn=void(*)(uintptr_t,int,const float*);
 __try{((AttribMenuFn)(image+AttribMenuRva))(windows,1,position);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void HeraldLevelUp(int k){
 uintptr_t player=PlayerCtrl();int hp=0;
 if(!heraldMenuLayoutReady||k<0||k>=SlotCount||strcmp(Roster[slots[k].npc].name,"Emerald Herald")||HeraldTalking(k)||!slots[k].active||!OwnedCompanion(player,slots[k])||GameStep()!=30||!ReadI32(player+0x168,hp)||hp<=0||!ReadI32(slots[k].ctrl+0x168,hp)||hp<=0)return;
 float pp[3],cp[3];if(!ReadVec(player+0x90,pp)||!ReadVec(slots[k].ctrl+0x90,cp)||Distance(pp,cp)>5.0f||GetTickCount64()-lastHeraldMenu<5000)return;
 int animation=ReadAnimation(player);if(animation<0||animation>=100000){Trace("herald_level_up_skipped",",\"reason\":\"player_busy\"");return;}
 uintptr_t ai=CompanionAi(player,slots[k]);int state=0;if(!ai||!ReadI32(ai+0x3a1,state)||(state&255)!=14){Trace("herald_level_up_skipped",",\"reason\":\"herald_busy\"");return;}
 uintptr_t gm=0,events=0,windows=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0x70,events)||!events||!ReadPtr(events+0x50,windows)||!windows)return;
 alignas(16) float position[8]={pp[0],pp[1],pp[2],1,0,0,0,0};
 // Native attribute menu, type 1. The game handles soul costs and confirmation.
 if(!CallAttributeMenu(windows,position))return;lastHeraldMenu=GetTickCount64();menuOpen=false;
 Trace("herald_level_up_requested");
}
