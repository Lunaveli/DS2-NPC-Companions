#pragma once
#include "rosabeth-profile-layout.h"
#include "rosabeth-profile-policy.h"
// Read the save's donation globals; update ONLY the private constructor profile.
// Never call the old crashing live equipment/model refresh APIs or write saves.
static bool ReadRosabethDonations(int* saved){
 uintptr_t gm=0,events=0,values=0,global=0,array=0,vt=0;int count=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x70,events)||!events||!ReadPtr(events+0x28,values)||!values||!ReadPtr(values+0x20,global)||!global||!ReadPtr(global,vt)||vt!=image+0x10f0398||!ReadPtr(global+8,array)||!UserPointer(array)||!ReadI32(global+0x10,count)||count!=256)return false;
 for(int i=0;i<4;i++)if(!ReadI32(array+(240+i)*4,saved[i]))return false;
 return true;
}
static bool WriteRosabethConstructorArmor(uintptr_t table,const int* armor){
 uintptr_t row=ParamRowData(table,990718);if(!row)return false;
 __try{if(memcmp((void*)(table+0x0c),"NPC_PLAYER_STATUS_PARAM",sizeof("NPC_PLAYER_STATUS_PARAM")-1))return false;
  memcpy((void*)(row+RosabethArmorOffset),armor,4*sizeof(int));return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void TickRosabethSpawnProfile(uintptr_t host,ULONGLONG now){
 static uintptr_t observedHost=0;static int previous[4]={};static ULONGLONG next=0,stableAt=0;static bool seen=false;
 if(now<next||!host||GameStep()!=30)return;next=now+500;int hp=0;if(!ReadI32(host+0x168,hp)||hp<=0)return;
 int saved[4],armor[4];if(!ReadRosabethDonations(saved)||!RosabethSpawnArmor(saved,armor))return;
 if(host!=observedHost||!seen||memcmp(previous,saved,sizeof(saved))){observedHost=host;memcpy(previous,saved,sizeof(saved));stableAt=now;seen=true;return;}
 if(now-stableAt<1000)return;
 uintptr_t gm=0,characters=0,container=0,table=0;
 if(!ReadPtr(image+GameManagerRva,gm)||!gm||!ReadPtr(gm+0x18,characters)||!characters||!ReadPtr(characters+0x750,container)||!container||!ReadPtr(container+0xd8,table)||!table)return;
 uintptr_t row=ParamRowData(table,990718);int current[4];if(!row)return;
 uintptr_t source=ParamRowData(table,7630);int original[4];if(!source)return;
 for(int i=0;i<4;i++)if(!ReadI32(source+RosabethArmorOffset+4*i,original[i]))return;
 if(!RosabethSpawnArmor(saved,armor,original))return;
 for(int i=0;i<4;i++)if(!ReadI32(row+RosabethArmorOffset+4*i,current[i]))return;
 if(!memcmp(current,armor,sizeof(current)))return;
 if(PlayerCtrl()!=host||GameStep()!=30||!WriteRosabethConstructorArmor(table,armor))return;
 char text[240];snprintf(text,sizeof(text),",\"armor\":[%d,%d,%d,%d],\"apply_at\":\"next_native_spawn\",\"live_equipment_call\":false",armor[0],armor[1],armor[2],armor[3]);Trace("rosabeth_spawn_outfit",text);
}
