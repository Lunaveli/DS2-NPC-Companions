#pragma once
// Companion HP options: passive regeneration, per-slot max HP scaling and group spawns.
constexpr int CompanionRegenPerSecond=15;
constexpr int MaxGroupSize=SpawnersPerSlot;   // one character per spawner; each slot owns SpawnersPerSlot spawners
static std::atomic<bool> companionRegen{false};
static std::atomic<float> companionHpScale[SlotCount]={{1.0f},{1.0f},{1.0f}};
static std::atomic<bool> groupEnabled[SlotCount]={{false},{false},{false}};
static std::atomic<int> groupSize[SlotCount]={{2},{2},{2}};
// Mob entries (Scholar enemy copies and the Desert Sorceress) may spawn as a group.
static bool GroupCapable(int npc){return npc>=0&&npc<RosterCount&&(!strncmp(Roster[npc].from,"Scholar",7)||!strncmp(Roster[npc].from,"Mob",3));}
static int GroupCount(int k){int n=groupSize[k].load();return GroupCapable(slots[k].npc)&&groupEnabled[k].load()?(std::max)(1,(std::min)(n,MaxGroupSize)):1;}
static void SaveCompanionHealth(){
 wchar_t key[24],text[32];
 WritePrivateProfileStringW(L"companions",L"passiveRegen",companionRegen.load()?L"1":L"0",ini.c_str());
 for(int k=0;k<SlotCount;k++){
  swprintf_s(key,L"hpScale%d",k+1);swprintf_s(text,L"%.1f",companionHpScale[k].load());WritePrivateProfileStringW(L"companions",key,text,ini.c_str());
  swprintf_s(key,L"group%d",k+1);WritePrivateProfileStringW(L"companions",key,groupEnabled[k].load()?L"1":L"0",ini.c_str());
  swprintf_s(key,L"groupSize%d",k+1);swprintf_s(text,L"%d",groupSize[k].load());WritePrivateProfileStringW(L"companions",key,text,ini.c_str());
 }
}
static void LoadCompanionHealth(){
 wchar_t key[24],text[32];
 companionRegen=GetPrivateProfileIntW(L"companions",L"passiveRegen",1,ini.c_str())!=0;
 for(int k=0;k<SlotCount;k++){
  swprintf_s(key,L"hpScale%d",k+1);GetPrivateProfileStringW(L"companions",key,L"1.0",text,32,ini.c_str());companionHpScale[k]=HpMultiplierSetting((float)_wtof(text));
  swprintf_s(key,L"group%d",k+1);groupEnabled[k]=GetPrivateProfileIntW(L"companions",key,0,ini.c_str())!=0;
  swprintf_s(key,L"groupSize%d",k+1);groupSize[k]=(std::max)(1,(std::min)((int)GetPrivateProfileIntW(L"companions",key,2,ini.c_str()),MaxGroupSize));
 }
}
// Per-actor state. Like world-health.h, the native max HP is rebased whenever the game itself changes it.
struct CompanionHealth {uintptr_t ctrl;int baseMax,lastMax;float scale;double regen;ULONGLONG regenAt;unsigned seen;};
static std::vector<CompanionHealth> companionHealth;
static unsigned companionHealthSerial=0;
static bool WriteCompanionHealth(uintptr_t ctrl,int hp,int maxHp,int newHp,int newMax){
 __try {
  if(*(int*)(ctrl+0x168)!=hp||*(int*)(ctrl+0x170)!=maxHp)return false;
  *(int*)(ctrl+0x170)=newMax;*(int*)(ctrl+0x168)=newHp;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Apply slot k's HP scale and regeneration to one living companion actor; hp/maxHp return the result.
static void UpdateCompanionHealth(int k,uintptr_t ctrl,ULONGLONG now,int& hp,int& maxHp){
 if(!ctrl||!ReadI32(ctrl+0x168,hp)||!ReadI32(ctrl+0x170,maxHp)||hp<=0||maxHp<=0||hp>maxHp)return;
 auto it=std::find_if(companionHealth.begin(),companionHealth.end(),[&](const CompanionHealth& e){return e.ctrl==ctrl;});
 if(it==companionHealth.end()){companionHealth.push_back({ctrl,maxHp,maxHp,1.0f,0,now,0});it=companionHealth.end()-1;}
 it->seen=companionHealthSerial;
 if(maxHp!=it->lastMax){it->baseMax=maxHp;it->lastMax=maxHp;it->scale=1.0f;}
 float scale=(std::min)(99.0f,ProgressionHpFactor(slots[k].npc));   // upgrade level = NG+ HP (no manual HP scale)
 if(it->scale!=scale){int newHp=0,newMax=0;
  if(ScaleHpValues(hp,maxHp,it->baseMax,scale,newHp,newMax)&&WriteCompanionHealth(ctrl,hp,maxHp,newHp,newMax)){
   Log("Slot %d companion level HP x%.2f: %d/%d -> %d/%d (native max %d)",k+1,scale,hp,maxHp,newHp,newMax,it->baseMax);
   hp=newHp;maxHp=newMax;it->lastMax=newMax;it->scale=scale;
  }
 }
 double seconds=(double)(std::min)(now-it->regenAt,(ULONGLONG)1000)/1000.0;it->regenAt=now;
 if(!companionRegen.load()||hp>=maxHp){it->regen=0;return;}
 it->regen+=CompanionRegenPerSecond*seconds;int gain=(int)it->regen;if(gain<=0)return;it->regen-=gain;
 int newHp=(std::min)(maxHp,hp+gain);if(WriteCompanionHealth(ctrl,hp,maxHp,newHp,maxHp))hp=newHp;
}
static void PruneCompanionHealth(){companionHealth.erase(std::remove_if(companionHealth.begin(),companionHealth.end(),[](const CompanionHealth& e){return e.seen!=companionHealthSerial;}),companionHealth.end());}
