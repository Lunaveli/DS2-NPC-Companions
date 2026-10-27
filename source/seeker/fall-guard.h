#pragma once
// 0.35 test: fall guard for live companion moves ([companions] fallGuard, F7 checkbox; remove this file and its four
// call sites to undo). 0.33 bridge crash: a placement during an area-data swap put Luet where collision was not yet
// loaded, she fell ~300 m, and the game crashed tearing her down. Moves are held while area data changes, and a
// companion that drops right after one of our moves is brought back to the player.
constexpr ULONGLONG FallGuardSettle=5000,FallGuardWatch=15000,FallGuardRescueWindow=60000;
constexpr float FallGuardDrop=12.0f;
constexpr int FallGuardRescues=3;
static std::atomic<bool> fallGuard{true};
static ULONGLONG fallGuardAreaChange=0,fallGuardRetry[SlotCount]={},fallGuardMoveAt[SlotCount]={},fallGuardWindow[SlotCount]={};
static float fallGuardTarget[SlotCount][3]={};
static int fallGuardMap=-1,fallGuardRescueCount[SlotCount]={};
static void TeleportSlot(int k,const char* why);
// True when a live move must wait for the area data to settle; a retry follows automatically.
static bool FallGuardHold(int k,const char* why){
 if(!fallGuard.load())return false;ULONGLONG now=GetTickCount64();
 if(now-fallGuardAreaChange>=FallGuardSettle)return false;
 if(!fallGuardRetry[k])Log("%s slot %d: move held by fall guard (area data changed %llu ms ago)",why,k+1,now-fallGuardAreaChange);
 fallGuardRetry[k]=now;slots[k].nextTeleport=now+2000;return true;
}
static void FallGuardMoved(int k,const float* target){memcpy(fallGuardTarget[k],target,12);fallGuardMoveAt[k]=GetTickCount64();}
static void FallGuardTick(const float* pp,ULONGLONG now){
 int map=currentMap.load();if(map!=fallGuardMap){fallGuardMap=map;fallGuardAreaChange=now;}
 if(!fallGuard.load()){for(auto& r:fallGuardRetry)r=0;return;}
 for(int k=0;k<SlotCount;k++){
  Slot& s=slots[k];
  if(fallGuardRetry[k]&&now-fallGuardAreaChange>=FallGuardSettle){fallGuardRetry[k]=0;if(s.active&&s.ctrl&&s.hp>0)TeleportSlot(k,"Fall guard retry");}
  if(!s.ctrl||s.hp<=0||!fallGuardMoveAt[k]||now-fallGuardMoveAt[k]>FallGuardWatch||s.pos[1]>fallGuardTarget[k][1]-FallGuardDrop)continue;
  if(now-fallGuardWindow[k]>FallGuardRescueWindow){fallGuardWindow[k]=now;fallGuardRescueCount[k]=0;}
  if(fallGuardRescueCount[k]>=FallGuardRescues){fallGuardMoveAt[k]=0;Log("Fall guard slot %d: rescue limit reached; leaving the companion to normal recovery",k+1);continue;}
  float spot[3]={pp[0]+1.0f+0.8f*k,pp[1]+0.3f,pp[2]+1.0f-0.8f*k};
  if(RequestPosition(s.ctrl,spot)){fallGuardRescueCount[k]++;Log("Fall guard slot %d: dropped %.1f m below the move target; brought back to the player (%d/%d)",k+1,fallGuardTarget[k][1]-s.pos[1],fallGuardRescueCount[k],FallGuardRescues);FallGuardMoved(k,spot);}
 }
}
