#pragma once
// One recall per continuous native boss-gauge session; short UI gaps / phases
// do not rearm it. Engine-independent so timing and reset behavior can be tested.
struct BossRoomPolicy {
 bool tracking=false,consumed=false,quiet=false;
 unsigned long long began=0,quietAt=0;
 void Reset(){*this=BossRoomPolicy();}
 bool Tick(unsigned long long now,bool signal,bool enabled,bool safe){
  if(!enabled){Reset();return false;}
  if(!signal){if(tracking){if(!quiet){quiet=true;quietAt=now;}if(now-quietAt>=5000)Reset();}return false;}
  quiet=false;
  if(!tracking){tracking=true;began=now;}
  if(!consumed&&safe&&now-began>=2000&&now-began<=30000){consumed=true;return true;}
  return false;
 }
};
