#pragma once
#include "recall-filter-policy.h"
static std::atomic<bool> conservativeRecall{true};
static ULONGLONG recallDeferredTrace[SlotCount]={};
static bool DeferAutomaticRecall(int k,const char* why,const float* hostPos,ULONGLONG now){
 if(!conservativeRecall.load()||!why)return false;
 RecallReason reason=RecallReason::Other;
 if(!strcmp(why,"Auto (emergency distance)"))reason=RecallReason::Emergency;
 else if(!strcmp(why,"Auto (vertical transition)"))reason=RecallReason::Vertical;
 else if(!strcmp(why,"Auto (far, stationary)")||!strcmp(why,"Auto (stuck, stationary)"))reason=RecallReason::Stationary;
 const Slot& s=slots[k];int flags=0;uintptr_t ai=CompanionAi(PlayerCtrl(),s);
 bool combat=ai&&ReadI32(ai+0x3a1,flags)&&(flags&255)==10;
 float height=fabsf(hostPos[1]-s.pos[1]);
 bool ladder=Roster[s.npc].model==1&&HumanLadderAnimation(ReadAnimation(s.ctrl));
 bool defer=DeferOptionalRecall(reason,s.distance,height,now-s.motionAt,now-s.verticalAt,combat,ladder);
 if(defer&&now>=recallDeferredTrace[k]){
  recallDeferredTrace[k]=now+10000;char text[256];
  snprintf(text,sizeof(text),",\"slot\":%d,\"reason\":%s,\"distance\":%.2f,\"height_gap\":%.2f,\"combat_order\":%s",k+1,JsonString(why).c_str(),s.distance,height,combat?"true":"false");Trace("recall_deferred",text);
 }
 return defer;
}
static bool DeferGroupRecall(int k,uintptr_t actor,float distance,const float* pos,ULONGLONG now){
 if(!conservativeRecall.load())return false;
 struct Motion {uintptr_t actor=0;float pos[3]={};ULONGLONG at=0;};static Motion motion[SlotCount][SpawnersPerSlot-1];
 Motion* track=nullptr;for(auto& m:motion[k])if(m.actor==actor){track=&m;break;}
 if(!track)for(auto& m:motion[k]){bool live=false;for(int e=0;e<slots[k].extraCount;e++)live|=slots[k].extras[e]==m.actor;if(!m.actor||!live){track=&m;m.actor=actor;memcpy(m.pos,pos,12);m.at=now;break;}}
 if(track&&Distance(pos,track->pos)>0.6f){memcpy(track->pos,pos,12);track->at=now;}
 bool ladder=Roster[slots[k].npc].model==1&&HumanLadderAnimation(ReadAnimation(actor));
 if(ladder&&track&&now-track->at<20000)return true;
 if(distance>60)return false;
 Slot member=slots[k];member.ctrl=actor;int flags=0;uintptr_t ai=CompanionAi(PlayerCtrl(),member);
 if(ai&&ReadI32(ai+0x3a1,flags)&&(flags&255)==10)return true;
 return distance<=40||!track||now-track->at<12000;
}
static std::atomic<bool> tunnelRecovery{true};
static bool TunnelFallbackNeeded(int k,const float* hostPos,ULONGLONG now){
 struct Observation {uintptr_t actor=0;int map=0;float host[3]={};ULONGLONG hostAt=0,window=0,last=0;int count=0;};static Observation observations[SlotCount];
 Slot& s=slots[k];auto& o=observations[k];
 if(!tunnelRecovery.load()||HeraldTalking(k)||!s.ctrl||s.hp<=0||(Roster[s.npc].model!=1&&!IsHerald(s.npc)))return false;
 if(o.actor!=s.ctrl||o.map!=GameMapId()){o=Observation();o.actor=s.ctrl;o.map=GameMapId();memcpy(o.host,hostPos,12);o.hostAt=now;return false;}
 if(Distance(o.host,hostPos)>0.5f){memcpy(o.host,hostPos,12);o.hostAt=now;}
 int flags=0;uintptr_t ai=CompanionAi(PlayerCtrl(),s);bool combat=ai&&ReadI32(ai+0x3a1,flags)&&(flags&255)==10;
 int hostAnimation=ReadAnimation(PlayerCtrl());bool ladder=Roster[s.npc].model==1&&HumanLadderAnimation(ReadAnimation(s.ctrl));
 if(!TunnelRescueNeeded(s.distance,fabsf(hostPos[1]-s.pos[1]),now-s.motionAt,now-o.hostAt,now-fallGuardAreaChange,combat,ladder,hostAnimation>=0&&hostAnimation<100000))return false;
 if(now-o.window>=60000){o.window=now;o.count=0;}
 if((o.last&&now-o.last<30000)||o.count>=2)return false;
 o.last=now;o.count++;char text[160];snprintf(text,sizeof(text),",\"slot\":%d,\"distance\":%.2f,\"stationary_ms\":%llu,\"attempt\":%d",k+1,s.distance,now-s.motionAt,o.count);Trace("tunnel_nav_fallback",text);return true;
}
