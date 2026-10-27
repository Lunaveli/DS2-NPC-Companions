#pragma once
#include "invader-sources.h"
// 0.44 progression: menu tabs, unlocks and titanite upgrades. Everything per character is stored in event flags of
// bank 20 (saved with the character): 207000-209999 is not declared or referenced by the game's scripts and is unset
// in existing saves. Boss kills come from the game's own boss counter table (SoulSplitter offsets).
static bool ReadFloat(uintptr_t at,float& out){__try{out=*(float*)at;return true;}__except(EXCEPTION_EXECUTE_HANDLER){out=0;return false;}}
static bool WriteFloat(uintptr_t at,float v){__try{*(float*)at=v;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
enum class NpcTab {White,Dark,Misc,Hidden};
constexpr uint32_t InvaderUnlockFlagBase=209000;   // + invader index (InvaderSources order)
constexpr uint32_t LevelFlagBase=209100;           // + roster index * 3, three bits = level 0-7
constexpr int MaxCompanionLevel=7;
constexpr int AgedFeather=60355000;
struct Material {int item;const char* name;};
constexpr Material Titanite[]={{60970000,"Titanite Shard"},{60975000,"Large Titanite Shard"},{60980000,"Titanite Chunk"},{60990000,"Titanite Slab"}};
struct UpgradeCost {int material,count;};
// Weapon-style: next level 1-2 shards, 3-4 large shards, 5-6 chunks, 7 slab.
constexpr UpgradeCost UpgradeCosts[MaxCompanionLevel+1]={{0,0},{0,1},{0,2},{1,1},{1,2},{2,1},{2,2},{3,1}};
struct BossInfo {int offset;const char* name;};
constexpr BossInfo Bosses[]={
 {0x7c,"The Last Giant"},{0x70,"The Pursuer"},{0x4c,"Dragonrider"},{0x50,"Old Dragonslayer"},{0x54,"Flexile Sentry"},
 {0x58,"Ruin Sentinels"},{0xa8,"Belfry Gargoyles"},{0x5c,"The Lost Sinner"},{0x38,"The Skeleton Lords"},{0x34,"Executioner's Chariot"},
 {0x3c,"Covetous Demon"},{0x40,"Mytha, the Baneful Queen"},{0x60,"Smelter Demon"},{0x30,"Old Iron King"},{0x44,"Scorpioness Najka"},
 {0x6c,"Royal Rat Authority"},{0xa4,"Prowling Magus and Congregation"},{0x2c,"The Duke's Dear Freja"},{0x64,"Royal Rat Vanguard"},{0x68,"The Rotten"},
 {0xa0,"Twin Dragonriders"},{0x48,"Looking Glass Knight"},{0x28,"Demon of Song"},{0x8c,"Velstadt, the Royal Aegis"},{0x98,"Vendrick"},
 {0x78,"Guardian Dragon"},{0x94,"Ancient Dragon"},{0x80,"Giant Lord"},{0x88,"Throne Watcher and Defender"},{0x84,"Nashandra"},
 {0x118,"Aldia, Scholar of the First Sin"},{0x9c,"Darklurker"},{0xc8,"Elana, the Squalid Queen"},{0xd4,"Sinh, the Slumbering Dragon"},
 {0xf4,"Graverobber, Varg and Cerah"},{0xfc,"Blue Smelter Demon"},{0xcc,"Fume Knight"},{0xf8,"Sir Alonne"},{0xd0,"Aava, the King's Pet"},
 {0x104,"Burnt Ivory King"},{0x108,"Lud and Zallen, the King's Pets"}};
// White summons: locked until the boss of the area where their first summon sign appears is defeated.
struct WhiteRule {const char* npc;int boss;};
constexpr WhiteRule WhiteRules[]={
 {"Abbess Feeva",0xd4},{"Ashen Knight Boyd",0x2c},{"Bashful Ray",0x2c},{"Bradley of the Old Guard",0x54},{"Devotee Scarlett",0x40},
 {"Felicia the Brave",0x58},{"Jester Thomas",0x40},{"Lone Hunter Schmidt",0x68},{"Manhunter O'Harrah",0x2c},{"Masterless Glencour",0x4c},
 {"Melinda the Butcher",0x94},{"Pilgrim Bellclaire",0x58},{"Rapacious Andrei",0xf4},{"Ruined Aflis",0x7c},{"Sellsword Luet",0x7c},
 {"Steelheart Ellie",0xd4},{"Transcendent Edde",0xd4},
 {"Lucatiel of Mirrah",0x5c},{"Benhart of Jugo",0x2c},{"Captain Drummond",0x80},{"Carhillion of the Fold",0xcc},{"Creighton the Wanderer",0x34},
 {"Drifter Swordsman Aidel",0x60},{"Grave Warden Agdayne",0x8c},{"Head of Vengarl",0x94},{"Manscorpion Tark",0x44},{"Mild Mannered Pate",0x70},
 {"Steel-willed Lorrie",0xcc},{"Twiggy Shei",0x108}};
static const char* BossName(int offset){for(const auto& b:Bosses)if(b.offset==offset)return b.name;return "an area boss";}

struct NpcProgress {NpcTab tab=NpcTab::Hidden;int boss=-1;int invader=-1;};
static NpcProgress npcProgress[RosterCount];
static void BuildProgressTable(){
 for(int i=0;i<RosterCount;i++){
  NpcProgress p;
  for(const auto& w:WhiteRules)if(!strcmp(Roster[i].name,w.npc)){p.tab=NpcTab::White;p.boss=w.boss;}
  for(int v=0;v<(int)(sizeof(InvaderSources)/sizeof(InvaderSources[0]));v++)if(Roster[i].enemy==InvaderSources[v].copy){p.tab=NpcTab::Dark;p.invader=v;}
  if(Roster[i].enemy==990712)p.tab=NpcTab::Misc;   // Emerald Herald (Fire Keeper slot)
  npcProgress[i]=p;
 }
}
// ---- Game state reads ----
static bool ReadFlag(uint32_t id){uintptr_t m=flagManager.load();return m&&originalFlag&&originalFlag(m,id)!=0;}
static uintptr_t BossCounters(){uintptr_t gm=0,a=0,b=0,c=0,t=0;if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0x70,a)||!ReadPtr(a+0x28,b)||!ReadPtr(b+0x20,c)||!ReadPtr(c+0x8,t))return 0;return t;}
static uintptr_t ItemList();
// Locks are only judged when every source can be read; an unreadable pointer must never dismiss companions.
static bool ProgressReady(){uintptr_t player=PlayerCtrl();int hp=0;return player&&GameStep()==30&&flagManager.load()&&originalFlag&&ReadI32(player+0x168,hp)&&BossCounters()&&ItemList();}
static bool BossDefeated(int offset){uintptr_t t=BossCounters();int n=0;return t&&ReadI32(t+offset,n)&&n>0;}
static int CurrentRound(){uintptr_t gm=0,a=0,misc=0;int r=0;if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0xa8,a)||!ReadPtr(a+0xc0,misc)||!ReadI32(misc+0x68,r))return 1;return r<1?1:r>8?8:r;}
// Inventory: [[[gm+0xa8]+0x10]+0x10] lists stacks (id, box, count, ...; 0x10 each from +0x30); [list+0x10] is the
// ItemInventory2BagList whose vtable slot 6 (+0x1b39c0) removes an item id/count across stacks the game's own way.
constexpr uintptr_t BagListVtableRva=0x10c4408,BagListRemoveRva=0x1b39c0;
constexpr unsigned char BagListRemoveBytes[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x20};
static uintptr_t ItemList(){uintptr_t gm=0,a=0,b=0,list=0;if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0xa8,a)||!ReadPtr(a+0x10,b)||!ReadPtr(b+0x10,list))return 0;return list;}
static int ItemCount(int item){
 uintptr_t list=ItemList(),end=0;if(!list||!ReadPtr(list+0x10,end)||end<=list||end-list>0x200000)return 0;int total=0;
 __try{for(uintptr_t e=list+0x30;e+0x10<=end;e+=0x10)if(*(int*)e==item&&*(int*)(e+4)==0&&*(int*)(e+8)>0)total+=*(int*)(e+8);}__except(EXCEPTION_EXECUTE_HANDLER){}
 return total;
}
static uintptr_t BagList(){uintptr_t list=ItemList(),bag=0,vt=0;if(!list||!ReadPtr(list+0x10,bag)||!ReadPtr(bag,vt)||vt!=image+BagListVtableRva)return 0;return bag;}
static bool BagListRemoveVerified(){__try{return memcmp((void*)(image+BagListRemoveRva),BagListRemoveBytes,sizeof(BagListRemoveBytes))==0&&*(uintptr_t*)(image+BagListVtableRva+6*8)==image+BagListRemoveRva;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
// Removal runs on the game thread (generator resource tick), never on the companion worker.
static std::atomic<int> removeItem{0},removeCount{0},removeState{0};   // state: 0 idle, 1 requested, 2 done, 3 failed
static void ProgressionGameThreadTick(){
 if(removeState.load()!=1)return;
 uintptr_t bag=BagList();int item=removeItem.load(),count=removeCount.load();
 if(!bag||!BagListRemoveVerified()||count<=0){removeState=3;return;}
 using RemoveFn=void(*)(uintptr_t,int,int);
 __try{((RemoveFn)(image+BagListRemoveRva))(bag,item,count);removeState=2;}__except(EXCEPTION_EXECUTE_HANDLER){removeState=3;}
}
// ---- Per-character state ----
static bool InvaderUnlocked(int v){return v>=0&&ReadFlag(InvaderUnlockFlagBase+v);}
static int NpcLevel(int npc){if(npc<0||npc>=RosterCount||npcProgress[npc].tab==NpcTab::Hidden)return 0;int l=0;for(int b=0;b<3;b++)if(ReadFlag(LevelFlagBase+npc*3+b))l|=1<<b;return l;}
static bool WriteLevel(int npc,int level){bool ok=true;for(int b=0;b<3;b++)ok&=SetRealFlag(LevelFlagBase+npc*3+b,(level>>b)&1)==1;return ok;}
// Why an NPC cannot be used now (nullptr = available). Hidden roster entries stay in the code but not in the menu.
static const char* LockReason(int npc,char* buffer,size_t size){
 if(npc<0||npc>=RosterCount)return "Unknown";
 const auto& p=npcProgress[npc];
 if(p.tab==NpcTab::Hidden)return "Not available in this version";
 if(!ProgressReady())return "Load your character first";
 if(p.tab==NpcTab::White&&!BossDefeated(p.boss)){snprintf(buffer,size,"Defeat %s",BossName(p.boss));return buffer;}
 if(p.tab==NpcTab::Dark&&!InvaderUnlocked(p.invader))return "Defeat this invader in your world";
 if(p.tab==NpcTab::Misc&&ItemCount(AgedFeather)<=0)return "Carry the Aged Feather";
 return nullptr;
}
static bool NpcAvailable(int npc){char b[96];return LockReason(npc,b,sizeof(b))==nullptr;}
// ---- Level effects: the game's own NG+ values (level N = NG+N, never below the current cycle) ----
static uintptr_t EnemyRow(int enemy){for(uintptr_t t:enemyTables){uintptr_t r=ParamRowData(t,(uint32_t)enemy);if(r)return r;}return 0;}
static int HpAtRound(uintptr_t row,int round){int hp=0;ReadI32(round<=1?row+40:row+320+4*(round-2),hp);return hp;}
static int RoundFor(int npc){int cur=CurrentRound(),l=NpcLevel(npc);return (std::min)(8,(std::max)(cur,l+1));}
static float ProgressionHpFactor(int npc){
 if(npc<0||npc>=RosterCount||npcProgress[npc].tab==NpcTab::Hidden)return 1.0f;int cur=CurrentRound(),eff=RoundFor(npc);if(eff==cur)return 1.0f;
 uintptr_t row=EnemyRow(Roster[npc].enemy);int a=row?HpAtRound(row,cur):0,b=row?HpAtRound(row,eff):0;return a>0&&b>0?(float)b/a:1.0f;
}
static float DamageFactor(int npc){
 int cur=CurrentRound(),eff=RoundFor(npc);if(eff==cur||scan.round.empty())return 1.0f;
 uintptr_t row=EnemyRow(Roster[npc].enemy);unsigned char cat=0;if(!row)return 1.0f;__try{cat=*(unsigned char*)(row+105);}__except(EXCEPTION_EXECUTE_HANDLER){return 1.0f;}
 for(uintptr_t t:scan.round){uintptr_t r=ParamRowData(t,cat);if(!r)continue;unsigned short a=0,b=0;__try{a=*(unsigned short*)(r+(cur-1)*6);b=*(unsigned short*)(r+(eff-1)*6);}__except(EXCEPTION_EXECUTE_HANDLER){}if(a&&b)return (float)b/a;}
 return 1.0f;
}
struct DamageRow {float base,applied;};
static std::vector<std::pair<uintptr_t,DamageRow>> damageRows;
// damageAdjustRateEnemy (+224) of the companion's loaded EnemyParam rows = native value x level factor.
static void ApplyLevelDamage(){
 for(int k=0;k<SlotCount;k++){int npc=slots[k].npc;if(npcProgress[npc].tab==NpcTab::Hidden)continue;float f=DamageFactor(npc);
  for(uintptr_t t:enemyTables){uintptr_t row=ParamRowData(t,(uint32_t)Roster[npc].enemy);float now=0;if(!row||!ReadFloat(row+224,now))continue;
   auto it=std::find_if(damageRows.begin(),damageRows.end(),[&](const auto& e){return e.first==row;});
   if(it==damageRows.end()){damageRows.push_back({row,{now,now}});it=damageRows.end()-1;}
   if(now!=it->second.applied)it->second.base=now;   // reloaded or first sight: current value is native
   float want=it->second.base*f;if(want!=now&&WriteFloat(row+224,want)){it->second.applied=want;}else it->second.applied=now;
  }}
}
// ---- Real invader kills unlock the Dark summon copy ----
struct WatchedInvader {uintptr_t ctrl;int invader,hp;unsigned seen;};
static std::vector<WatchedInvader> watchedInvaders;
static void WatchInvaderKills(uintptr_t player){
 static unsigned serial=0;unsigned now=++serial;uintptr_t gm=0,ai=0,node=0;if(!ReadPtr(image+GameManagerRva,gm)||!ReadPtr(gm+0x28,ai)||!ReadPtr(ai+0x10,node))return;
 for(int guard=0;node&&guard<1024;guard++){
  uintptr_t ctrl=0,next=0,param=0;ReadPtr(node+0x38,ctrl);ReadPtr(node+0x10,next);int hp=0;
  if(ctrl&&ctrl!=player&&ReadPtr(ctrl+0x38,param)&&ReadI32(ctrl+0x168,hp))
   for(int v=0;v<(int)(sizeof(InvaderSources)/sizeof(InvaderSources[0]));v++){
    bool real=false;for(uintptr_t t:enemyTables)if(ParamRowData(t,(uint32_t)InvaderSources[v].real)==param)real=true;if(!real)continue;
    auto it=std::find_if(watchedInvaders.begin(),watchedInvaders.end(),[&](const auto& w){return w.ctrl==ctrl;});
    if(it==watchedInvaders.end()){watchedInvaders.push_back({ctrl,v,hp,now});break;}
    it->seen=now;
    if(it->hp>0&&hp<=0&&!InvaderUnlocked(v)){int r=SetRealFlag(InvaderUnlockFlagBase+v,true);for(int i=0;i<RosterCount;i++)if(npcProgress[i].invader==v)Log("Dark summon unlocked: %s (real invader defeated, flag %u result %d)",NpcDisplayName(i),InvaderUnlockFlagBase+v,r);}
    it->hp=hp;break;
   }
  if(next==node)break;node=next;
 }
 watchedInvaders.erase(std::remove_if(watchedInvaders.begin(),watchedInvaders.end(),[&](const auto& w){return w.seen!=now;}),watchedInvaders.end());
}
static void SetActive(int k,bool on);
// Locked or hidden companions are dismissed (the overhaul respects locks for existing slots too).
static void EnforceLocks(){
 if(!ProgressReady())return;char b[96];
 for(int k=0;k<SlotCount;k++)if(slots[k].active){const char* why=LockReason(slots[k].npc,b,sizeof(b));if(why){Log("Slot %d (%s) dismissed: locked (%s)",k+1,NpcDisplayName(slots[k].npc),why);SetActive(k,false);}}
}
// ---- Upgrades (companion worker; item removal happens on the game thread) ----
static std::atomic<int> upgradeMessageNpc{-1};static char upgradeMessage[160]="";
static void UpgradeNpc(int npc){
 auto say=[&](const char* text){snprintf(upgradeMessage,sizeof(upgradeMessage),"%s: %s",NpcDisplayName(npc),text);upgradeMessageNpc=npc;Log("Upgrade %s",upgradeMessage);};
 if(!NpcAvailable(npc)){say("locked");return;}
 int level=NpcLevel(npc);if(level>=MaxCompanionLevel){say("already level 7");return;}
 const auto& cost=UpgradeCosts[level+1];const auto& m=Titanite[cost.material];int have=ItemCount(m.item);
 if(have<cost.count){char t[120];snprintf(t,sizeof(t),"needs %d x %s (you have %d)",cost.count,m.name,have);say(t);return;}
 removeItem=m.item;removeCount=cost.count;removeState=1;
 for(int i=0;i<60&&removeState.load()==1;i++)Sleep(25);
 int after=ItemCount(m.item),state=removeState.exchange(0);
 if(state!=2||after!=have-cost.count){char t[140];snprintf(t,sizeof(t),"titanite was not taken (state %d, %d -> %d); level unchanged",state,have,after);say(t);return;}
 if(!WriteLevel(npc,level+1)){say("titanite taken but the level flag write failed; see log");return;}
 char t[120];snprintf(t,sizeof(t),"level %d (used %d x %s)",level+1,cost.count,m.name);say(t);
}
static void ProgressionTick(uintptr_t player){if(!ProgressReady())return;WatchInvaderKills(player);EnforceLocks();ApplyLevelDamage();}
