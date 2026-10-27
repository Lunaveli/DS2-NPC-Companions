#pragma once
#include "boss-room-policy.h"
static std::atomic<bool> autoBossRecall{true};
static std::atomic<ULONGLONG> bossRoomPulse{0};
static std::atomic<uintptr_t> bossRoomHost{0};
static std::atomic<int> bossRoomMap{0};
static int ReadAnimation(uintptr_t actor);
// Called on the UI thread AFTER the original update. Never retains a scene or
// actor pointer for dereferencing elsewhere and never moves actors on this thread.
static void ObserveBossRoomGauge(uintptr_t scene){
 if(!autoBossRecall.load())return;
 __try {
  if(*(uintptr_t*)scene!=image+0x10b0c68||!*(unsigned char*)(scene+0xd5)
    ||*(int*)(scene+0xd8)<=0||*(int*)(scene+0xdc)<=0)return;
  uintptr_t host=PlayerCtrl();int hp=0;
  if(GameStep()!=30||!host||!ReadI32(host+0x168,hp)||hp<=0)return;
  bossRoomHost.store(host);bossRoomMap.store(GameMapId());
  bossRoomPulse.store(GetTickCount64(),std::memory_order_release);
 }__except(EXCEPTION_EXECUTE_HANDLER){}
}
static void TickBossRoomRecall(uintptr_t player,ULONGLONG now){
 static BossRoomPolicy policy;static uintptr_t host=0;static int map=0;
 static ULONGLONG pendingUntil=0;static bool pending[SlotCount]={};
 int current=GameMapId(),hp=0;
 if(!player||GameStep()!=30||!ReadI32(player+0x168,hp)||hp<=0||host!=player||map!=current){
  policy.Reset();pendingUntil=0;for(auto& p:pending)p=false;host=player;map=current;
  if(!player||GameStep()!=30||hp<=0)return;
 }
 ULONGLONG pulse=bossRoomPulse.load(std::memory_order_acquire);
 bool signal=pulse&&now>=pulse&&now-pulse<=1000&&bossRoomHost.load()==player&&bossRoomMap.load()==current;
 int animation=ReadAnimation(player);
 bool safe=!menuOpen.load()&&animation>=0&&animation<100000&&now-fallGuardAreaChange>=FallGuardSettle;
 bool wasTracking=policy.tracking;
 if(policy.Tick(now,signal,autoBossRecall.load(),safe)){
  pendingUntil=now+20000;
  for(int k=0;k<SlotCount;k++)pending[k]=slots[k].active&&!HeraldBlockedHere(slots[k].npc);
  Trace("boss_room_recall_ready");
 }
 if(!wasTracking&&policy.tracking)Trace("boss_room_detected",",\"source\":\"visible_native_boss_gauge\"");
 if(!autoBossRecall.load()||!policy.tracking||!signal||!safe||now>pendingUntil)return;
 for(int k=0;k<SlotCount;k++)if(pending[k]){
  Slot& s=slots[k];
  if(!s.active||HeraldBlockedHere(s.npc)){pending[k]=false;continue;}
  // Live actors only: missing/dead companions retain their normal recovery path.
  if(HeraldTalking(k)||!s.ctrl||s.hp<=0||!OwnedCompanion(player,s))continue;
  int actorHp=0;if(!ReadI32(s.ctrl+0x168,actorHp)||actorHp<=0)continue;
  if(PlayerCtrl()!=player||GameStep()!=30||!ReadI32(player+0x168,hp)||hp<=0)return;
  ULONGLONG before=s.recallAt;TeleportSlot(k,"Auto (boss room entry)");
  if(s.recallAt!=before){pending[k]=false;char text[96];snprintf(text,sizeof(text),",\"slot\":%d,\"group_members\":%d",k+1,s.extraCount);Trace("boss_room_companion_brought",text);}
 }
}
