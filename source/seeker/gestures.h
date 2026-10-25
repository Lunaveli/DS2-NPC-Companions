#pragma once
static std::atomic<bool> mirrorGestures{true};
static bool gestureLayoutReady=false;
constexpr uintptr_t NativeNpcAiVtableRva=0x10beb08;
// Native ItemParam gesture IDs. Bow was observed as animation 74010030 in the live sampler.
static int GestureItem(int animation) {
 if(animation<74010000||animation>74010310)return 0;
 switch(animation-74010000){
  case 10:return 63000000;case 20:return 63001000;case 30:return 63003000;case 40:return 63004000;
  case 50:return 63005000;case 60:return 63006000;case 240:return 63007000;case 70:return 63008000;
  case 280:return 63009000;case 270:return 63010000;case 120:return 63011000;case 80:return 63012000;
  case 90:return 63013000;case 100:return 63014000;case 130:return 63015000;case 250:return 63016000;
  case 150:return 63017000;case 310:return 63018000;case 220:return 63019000;case 300:return 63021000;
  default:return 0;
 }
}
static int ReadAnimation(uintptr_t actor) {
 uintptr_t p=actor;for(unsigned offset:{0xf8u,0x38u,0x78u,0x20u})if(!ReadPtr(p+offset,p)||!UserPointer(p))return -1;
 int id=-1;return ReadI32(p+0x10,id)?id:-1;
}
struct PendingGesture {uintptr_t actor=0;int animation=0,item=0;ULONGLONG due=0,expires=0,verify=0;bool queued=false,herald=false;uintptr_t node=0;int heraldStage=0;};
struct NativeGestureTicket {uintptr_t host=0,actor=0,node=0,param=0;int item=0,state=0;ULONGLONG expires=0;};
static NativeGestureTicket nativeGestures[SlotCount];
static SRWLOCK gestureLock=SRWLOCK_INIT;
static bool ValidGestureItem(int item){for(int animation=74010010;animation<=74010310;animation+=10)if(GestureItem(animation)==item&&item!=0)return true;return false;}
static PendingGesture pendingGestures[SlotCount];
static int observedGesture=0;static ULONGLONG nonGestureSince=0,lastGestureRequest=0;
static void ResetGestures(){for(int k=0;k<SlotCount;k++){SetRealFlag(HeraldGestureFlags[k],false);pendingGestures[k]=PendingGesture();}AcquireSRWLockExclusive(&gestureLock);for(auto& t:nativeGestures)t=NativeGestureTicket();ReleaseSRWLockExclusive(&gestureLock);observedGesture=0;nonGestureSince=0;lastGestureRequest=0;}
static uintptr_t CompanionAi(uintptr_t player,const Slot& slot) {
 if(!OwnedCompanion(player,slot))return 0;uintptr_t gm=0,ai=0,node=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0x28,ai)||!ReadPtr(ai+0x10,node))return 0;
 for(int i=0;node&&i<1024;i++){uintptr_t ctrl=0,next=0;if(!ReadPtr(node+0x38,ctrl))return 0;if(ctrl==slot.ctrl)return node;if(!ReadPtr(node+0x10,next)||next==node)break;node=next;}return 0;
}
static bool RequestGestureInput(uintptr_t actor,uintptr_t node,int item) {
 if(!gestureLayoutReady||!actor||!node||!ValidGestureItem(item))return false;
 __try {
  if(*(uintptr_t*)node!=image+NativeNpcAiVtableRva||*(uintptr_t*)(node+0x38)!=actor||*(int*)(actor+0x168)<=0)return false;
  // The native NpcUseGesture consumer at +0x126363 forwards to +0x1fc2a0.
  // That writer stores the selected gesture item at input+0x5c, then ORs bit 0 at +0xd.
  uintptr_t input=*(uintptr_t*)(node+0xa80);if(!UserPointer(input)||*(uintptr_t*)input!=actor)return false;
  if(*(unsigned char*)(input+0xd)&1)return false;
  *(int*)(input+0x5c)=item;_InterlockedOr8((char*)(input+0xd),1);return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Called after the native AI has rebuilt this frame's input. Never call engine AI
// functions from the companion worker, and never wait for its lock on a game thread.
static void AfterNativeNpcInput(uintptr_t node) {
 if(!mirrorGestures.load()||!gestureLayoutReady||!TryAcquireSRWLockExclusive(&gestureLock))return;
 ULONGLONG now=GetTickCount64();
 for(auto& t:nativeGestures)if(t.state==1&&t.node==node){
  int hp=0,flags=0;uintptr_t actor=0,param=0;
  if(now>t.expires||PlayerCtrl()!=t.host||GameStep()!=30||!ReadI32(t.host+0x168,hp)||hp<=0||!ReadPtr(node+0x38,actor)||actor!=t.actor||!ReadPtr(actor+0x38,param)||param!=t.param){t.state=3;continue;}
  int animation=ReadAnimation(actor);
  if(!ReadI32(node+0x3a1,flags)||(flags&255)!=14||animation<0||animation>=100000)continue;
  if(RequestGestureInput(actor,node,t.item))t.state=2;
 }
 ReleaseSRWLockExclusive(&gestureLock);
}
static void QueueNativeGesture(int k,uintptr_t host,uintptr_t actor,uintptr_t node,int item,ULONGLONG expires){
 uintptr_t param=0;if(!ReadPtr(actor+0x38,param))return;
 AcquireSRWLockExclusive(&gestureLock);nativeGestures[k]={host,actor,node,param,item,1,expires};ReleaseSRWLockExclusive(&gestureLock);
}
static void EndHeraldGesture(int k,const char* reason,bool started=false){
 auto& p=pendingGestures[k];SetRealFlag(HeraldGestureFlags[k],false);
 char b[240];snprintf(b,sizeof(b),",\"slot\":%d,\"player_animation\":%d,\"native_action\":7,\"native_action_started\":%s,\"reason\":\"%s\"",k+1,p.animation,started?"true":"false",reason);Trace("herald_gesture_response",b);p=PendingGesture();
}
static void UpdateHeraldGesture(int k,uintptr_t player,ULONGLONG now){
 auto& p=pendingGestures[k];
 if(HeraldBlockedHere(slots[k].npc)||HeraldServiceBusy(k)){EndHeraldGesture(k,"location_or_service_busy");return;}
 uintptr_t node=CompanionAi(player,slots[k]);int flags=0,hp=0;
 if(!node||!ReadI32(p.actor+0x168,hp)||hp<=0){EndHeraldGesture(k,"ownership_or_life_changed");return;}
 if(p.queued){
  if(node!=p.node){EndHeraldGesture(k,"ai_owner_changed");return;}
  int state=0;if(ReadI32(node+0x3a8,state)){
   state&=255;
   if(state==1)p.heraldStage=1;
   if(state==2)p.heraldStage=2;
   if(state==3&&p.heraldStage){EndHeraldGesture(k,"native_response_completed",true);return;}
   if(state==4&&p.heraldStage){EndHeraldGesture(k,"native_ai_busy");return;}
  }
  if(now>=p.verify)EndHeraldGesture(k,"native_response_timeout",p.heraldStage==2);
  return;
 }
 if(now>p.expires){EndHeraldGesture(k,"idle_window_expired");return;}
 if(now<p.due||!ReadI32(node+0x3a1,flags)||(flags&255)!=14)return;
 // Model 7005 has no player gesture clips or player animation chain. Ask its
 // custom lower AI to play a bounded, non-damaging native response instead.
 // The AI independently checks grounding/startup/damage and yields to Talk.
 if(SetRealFlag(HeraldGestureFlags[k],true)==1){p.queued=true;p.node=node;p.verify=now+6500;p.heraldStage=0;}
 else EndHeraldGesture(k,"request_flag_unavailable");
}
static void UpdateGestures(uintptr_t player,ULONGLONG now) {
 if(!mirrorGestures.load()||!gestureLayoutReady){ResetGestures();return;}
 int hostHp=0;if(PlayerCtrl()!=player||GameStep()!=30||!ReadI32(player+0x168,hostHp)||hostHp<=0){ResetGestures();return;}
 int animation=ReadAnimation(player),item=GestureItem(animation);
 if(item){
  nonGestureSince=0;
  if(observedGesture!=animation&&now-lastGestureRequest>=1200){
   observedGesture=animation;lastGestureRequest=now;
   AcquireSRWLockExclusive(&gestureLock);for(auto& t:nativeGestures)t=NativeGestureTicket();ReleaseSRWLockExclusive(&gestureLock);
   for(int k=0;k<SlotCount;k++){auto& p=pendingGestures[k];if(p.herald&&p.queued)continue;p=PendingGesture();if(slots[k].active&&slots[k].ctrl&&slots[k].hp>0&&(Roster[slots[k].npc].model==1||IsHerald(slots[k].npc))){p={slots[k].ctrl,animation,item,now+150+200*k,now+2200,0};p.herald=IsHerald(slots[k].npc);}}
   char b[120];snprintf(b,sizeof(b),",\"animation\":%d,\"gesture_item\":%d",animation,item);Trace("player_gesture",b);
  }
 }else if(animation>=0){if(!nonGestureSince)nonGestureSince=now;else if(now-nonGestureSince>=400)observedGesture=0;}
 // An unreadable animation link is a transient observation, not a new gesture edge.
 for(int k=0;k<SlotCount;k++){
  auto& p=pendingGestures[k];if(!p.actor)continue;
  if(slots[k].ctrl!=p.actor||!slots[k].active||slots[k].hp<=0||p.herald!=IsHerald(slots[k].npc)){if(p.herald)EndHeraldGesture(k,"slot_changed");else p=PendingGesture();continue;}
  if(p.herald){UpdateHeraldGesture(k,player,now);continue;}
  if(p.verify){int actual=ReadAnimation(p.actor);if(actual==p.animation||now>=p.verify){char b[180];snprintf(b,sizeof(b),",\"slot\":%d,\"requested_animation\":%d,\"observed_animation\":%d,\"played\":%s",k+1,p.animation,actual,actual==p.animation?"true":"false");Trace("gesture_mirror_result",b);p=PendingGesture();}continue;}
  if(p.queued){
   AcquireSRWLockExclusive(&gestureLock);int state=nativeGestures[k].state;if(state==2||state==3||now>p.expires)nativeGestures[k]=NativeGestureTicket();ReleaseSRWLockExclusive(&gestureLock);
   if(state==2){char b[180];snprintf(b,sizeof(b),",\"slot\":%d,\"animation\":%d,\"gesture_item\":%d,\"native_request\":true,\"on_native_update\":true",k+1,p.animation,p.item);Trace("gesture_mirror_request",b);p.verify=now+2400;continue;}
   if(state==3){Trace("gesture_mirror_skipped",",\"reason\":\"ownership_or_loading_changed\"");p=PendingGesture();continue;}
  }
  if(now>p.expires){uintptr_t n=CompanionAi(player,slots[k]);int f=-1;if(n)ReadI32(n+0x3a1,f);char b[220];snprintf(b,sizeof(b),",\"slot\":%d,\"reason\":\"busy_or_native_update_unavailable\",\"shared_flag_0\":%d,\"observed_animation\":%d,\"queued\":%s",k+1,f&255,ReadAnimation(p.actor),p.queued?"true":"false");Trace("gesture_mirror_skipped",b);p=PendingGesture();continue;}if(p.queued||now<p.due)continue;
  uintptr_t node=CompanionAi(player,slots[k]);int flags=0;
  // Shared flag 0 = 14 is native following/idle; combat, defense and startup get priority.
  if(!node||!ReadI32(node+0x3a1,flags)||(flags&255)!=14)continue;
  int currentAnim=ReadAnimation(p.actor);if(currentAnim<0||currentAnim>=100000)continue;
  if(PlayerCtrl()!=player||GameStep()!=30||!ReadI32(player+0x168,hostHp)||hostHp<=0)return;
  QueueNativeGesture(k,player,p.actor,node,p.item,p.expires);p.queued=true;
 }
}
