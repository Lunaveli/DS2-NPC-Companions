#pragma once
// Extra recall filtering only. Original RecallPolicy/ObserveRecall remain unchanged.
enum class RecallReason { Other, Stationary, Vertical, Emergency };
static bool HumanLadderAnimation(int animation){return (animation>=71010000&&animation<71040000)||(animation>=70040000&&animation<70050000);}
static bool DeferOptionalRecall(RecallReason reason,float distance,float heightGap,
 unsigned long long idleMs,unsigned long long stableHeightMs,bool combat,bool ladder=false){
 if(ladder&&idleMs<20000&&reason!=RecallReason::Other)return true;
 if(reason==RecallReason::Other||reason==RecallReason::Emergency||distance>60.0f)return false;
 if(combat&&heightGap<24.0f)return true;
 if(reason==RecallReason::Stationary&&distance<=30.0f&&idleMs<30000)return true;
 if(reason==RecallReason::Vertical&&heightGap<20.0f&&(idleMs<12000||stableHeightMs<12000))return true;
 return false;
}
static bool TunnelRescueNeeded(float distance,float heightGap,unsigned long long idleMs,
 unsigned long long hostIdleMs,unsigned long long settledMs,bool combat,bool ladder,bool safeHostPose){
 return distance>=6.0f&&distance<=18.0f&&heightGap<3.0f&&idleMs>=12000&&hostIdleMs>=5000
  &&settledMs>=5000&&!combat&&!ladder&&safeHostPose;
}
