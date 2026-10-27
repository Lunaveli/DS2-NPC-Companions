#pragma once
static std::atomic<bool> soulsMultiplierEnabled{false},infiniteDurability{false};
static std::atomic<float> soulsMultiplierSelected{3.0f};
static std::atomic<unsigned> soulsBoosts{0},durabilityBlocks{0};
static std::atomic<int> soulsLastBase{0},soulsLastBonus{0};
static void SavePlayerOptions(){
 wchar_t text[32];swprintf_s(text,L"%.1f",soulsMultiplierSelected.load());WritePrivateProfileStringW(L"cheats",L"soulsMultiplier",text,ini.c_str());
 WritePrivateProfileStringW(L"cheats",L"soulsMultiplierEnabled",soulsMultiplierEnabled.load()?L"1":L"0",ini.c_str());WritePrivateProfileStringW(L"cheats",L"infiniteDurability",infiniteDurability.load()?L"1":L"0",ini.c_str());
}
static void LoadPlayerOptions(){
 wchar_t text[64];GetPrivateProfileStringW(L"cheats",L"soulsMultiplier",L"3.0",text,64,ini.c_str());soulsMultiplierSelected=HpMultiplierSetting((float)_wtof(text),3.0f);
 soulsMultiplierEnabled=GetPrivateProfileIntW(L"cheats",L"soulsMultiplierEnabled",0,ini.c_str())!=0;infiniteDurability=GetPrivateProfileIntW(L"cheats",L"infiniteDurability",0,ini.c_str())!=0;
}
constexpr int SoulLimit=999999999; // native +0x38ab40 uses this cap
static int MultipliedSoulTotal(int before,int after,float multiplier){
 if(before<0||after<0||before>SoulLimit||after>SoulLimit||after<=before||!ValidHpMultiplier(multiplier)||multiplier<=1)return after;
 double total=before+round((double)(after-before)*multiplier);return total>=SoulLimit?SoulLimit:(int)total;
}
static uintptr_t LivingGameplayPlayer(){uintptr_t player=PlayerCtrl();int hp=0;return player&&GameStep()==30&&ReadI32(player+0x168,hp)&&hp>0?player:0;}
using GiveSoulsFn=void(*)(uintptr_t,uint32_t);
static GiveSoulsFn originalGiveSouls;
static void HookGiveSouls(uintptr_t bank,uint32_t amount){
 uintptr_t player=LivingGameplayPlayer(),ownedBank=0,owner=0;int before=0;
 float multiplier=soulsMultiplierSelected.load();
 bool apply=soulsMultiplierEnabled.load()&&multiplier>1&&player&&ReadPtr(player+0x490,ownedBank)&&ownedBank==bank&&ReadPtr(bank,owner)&&owner==player&&ReadI32(bank+0xec,before);
 originalGiveSouls(bank,amount);
 // Match the user's SoulsRealWriteHook CT: multiply increases to carried
 // souls only. Native Soul Memory, losses, costs and other counters stay native.
 if(apply&&LivingGameplayPlayer()==player){int after=0;if(!ReadI32(bank+0xec,after))return;int result=MultipliedSoulTotal(before,after,multiplier);
  if(result>after){__try{if(_InterlockedCompareExchange((volatile long*)(bank+0xec),result,after)==after){soulsLastBase=after-before;soulsLastBonus=result-after;soulsBoosts++;}}__except(EXCEPTION_EXECUTE_HANDLER){}}
 }
}
using DurabilityDamageFn=bool(*)(uintptr_t,int,float,unsigned char);
static DurabilityDamageFn originalDurabilityDamage;
static bool HookDurabilityDamage(uintptr_t action,int slot,float change,unsigned char mode){
 uintptr_t owner=0,player=LivingGameplayPlayer();
 // +0x1f8710 owns a ChrCtrl at +8 and applies a signed change to the
 // selected equipped item. Preserve positive repair/rest effects and NPCs.
 if(infiniteDurability.load()&&player&&std::isfinite(change)&&change<0&&ReadPtr(action+8,owner)&&owner==player){change=0;durabilityBlocks++;}
 return originalDurabilityDamage(action,slot,change,mode);
}
static void PlayerOptionsTelemetry(){
 static unsigned lastSouls=0,lastDurability=0;unsigned souls=soulsBoosts.load(),durability=durabilityBlocks.load();
 if(souls!=lastSouls||durability!=lastDurability){char text[256];snprintf(text,sizeof(text),",\"souls_boosts\":%u,\"last_base_gain\":%d,\"last_bonus\":%d,\"souls_multiplier\":%.1f,\"durability_losses_blocked\":%u",souls,soulsLastBase.load(),soulsLastBonus.load(),soulsMultiplierEnabled.load()?soulsMultiplierSelected.load():1.0f,durability);Trace("player_options",text);lastSouls=souls;lastDurability=durability;}
}
