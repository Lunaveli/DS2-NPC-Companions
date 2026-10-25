#pragma once
#include <limits.h>
struct ScaledEnemy {uintptr_t ctrl,param;int baseMax,lastMax;float multiplier;unsigned seen;int healthAnchor,maxAnchor,lastHp;};
static std::vector<ScaledEnemy> scaledEnemies;
static bool ScaleHpValues(int hp,int maxHp,int baseMax,float multiplier,int& newHp,int& newMax) {
 if(hp<=0||maxHp<=0||hp>maxHp||baseMax<=0||!ValidHpMultiplier(multiplier))return false;
 double desired=round((double)baseMax*multiplier);if(desired>INT_MAX||desired<1)return false;int64_t target=(int64_t)desired;
 newMax=(int)target;newHp=(int)((int64_t)hp*target/maxHp);if(newHp<1)newHp=1;return true;
}
static bool KnownHostile(uintptr_t param) {
 int group=0;if(!param||!ReadI32(param+0x14,group)||group!=1)return false;
 // Membership in a discovered CHR_PARAM excludes unrelated readable pointers.
 for(uintptr_t t:enemyTables){
  __try {unsigned count=*(uint16_t*)(t+0xa);for(unsigned i=0;i<count;i++){
   uintptr_t e=t+0x40+i*24;uint64_t off=*(uint64_t*)(e+8);uintptr_t p=off<0x4000000?t+off:(uintptr_t)off;if(p==param)return true;
  }}__except(EXCEPTION_EXECUTE_HANDLER){}
 }return false;
}
static bool WriteActorHealth(uintptr_t ctrl,uintptr_t param,int hp,int maxHp,int newHp,int newMax) {
 __try {
  if(*(uintptr_t*)(ctrl+0x38)!=param||*(int*)(ctrl+0x168)!=hp||*(int*)(ctrl+0x170)!=maxHp)return false;
  *(int*)(ctrl+0x170)=newMax;*(int*)(ctrl+0x168)=newHp;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void UpdateWorldHealth(uintptr_t player) {
 static unsigned serial=0;static float lastMode=-1;float mode=enemyHpMultiplier.load();
 if(lastMode!=mode){lastMode=mode;Log("World enemy HP multiplier: x%.1f (HP only)",mode);}
 if(mode==1&&scaledEnemies.empty())return;
 int hostHp=0;if(PlayerCtrl()!=player||GameStep()!=30||!ReadI32(player+0x168,hostHp)||hostHp<=0)return;
 uintptr_t gm=0,ai=0,node=0;if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x28,ai)||!ai||!ReadPtr(ai+0x10,node))return;
 unsigned current=++serial;int changed=0;std::string samples;
 for(int guard=0;node&&guard<1024;guard++){
  uintptr_t ctrl=0,next=0,param=0;ReadPtr(node+0x38,ctrl);ReadPtr(node+0x10,next);
  bool companion=false;for(const auto& s:slots){if(s.ctrl==ctrl)companion=true;for(int e=0;e<s.extraCount;e++)if(s.extras[e]==ctrl)companion=true;}
  int hp=0,maxHp=0;
  if(ctrl&&ctrl!=player&&!companion&&ReadPtr(ctrl+0x38,param)&&KnownHostile(param)&&ReadI32(ctrl+0x168,hp)&&ReadI32(ctrl+0x170,maxHp)&&hp>0&&maxHp>0){
   auto it=std::find_if(scaledEnemies.begin(),scaledEnemies.end(),[&](const ScaledEnemy& e){return e.ctrl==ctrl&&e.param==param;});
   if(it==scaledEnemies.end()){if(mode==1){node=next;continue;}scaledEnemies.push_back({ctrl,param,maxHp,maxHp,1,current,hp,maxHp,hp});it=scaledEnemies.end()-1;}
   it->seen=current;
   if(maxHp!=it->lastMax){it->baseMax=maxHp;it->lastMax=maxHp;it->multiplier=1;it->healthAnchor=hp;it->maxAnchor=maxHp;it->lastHp=hp;}
   // Slider movement must not compound integer rounding into free damage.
   // Rebase the health fraction only when native damage/healing changes HP.
   if(hp!=it->lastHp){it->healthAnchor=hp;it->maxAnchor=maxHp;it->lastHp=hp;}
   if(it->multiplier!=mode){int newHp=0,newMax=0;
    if(ScaleHpValues(it->healthAnchor,it->maxAnchor,it->baseMax,mode,newHp,newMax)&&PlayerCtrl()==player&&GameStep()==30&&WriteActorHealth(ctrl,param,hp,maxHp,newHp,newMax)){
     if(changed<6){char b[256];snprintf(b,sizeof(b),"%s{\"actor\":\"0x%llx\",\"old_hp\":%d,\"old_max_hp\":%d,\"base_max_hp\":%d,\"new_hp\":%d,\"new_max_hp\":%d}",changed?",":"",(unsigned long long)ctrl,hp,maxHp,it->baseMax,newHp,newMax);samples+=b;}
     it->lastMax=newMax;it->lastHp=newHp;it->multiplier=mode;changed++;
    }
   }
  }
  if(next==node)break;node=next;
 }
 scaledEnemies.erase(std::remove_if(scaledEnemies.begin(),scaledEnemies.end(),[&](const ScaledEnemy& e){return e.seen!=current||(mode==1&&e.multiplier==1);}),scaledEnemies.end());
 if(changed){char b[160];snprintf(b,sizeof(b),",\"multiplier\":%.1f,\"actors_updated\":%d,\"tracked\":%d,\"samples\":[",mode,changed,(int)scaledEnemies.size());Trace("world_hp_scaled",std::string(b)+samples+"]");}
}
