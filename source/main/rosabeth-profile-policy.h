#pragma once
static bool RosabethSpawnArmor(const int* saved,int* armor,const int* original=nullptr){
 bool donated=false;for(int i=0;i<4;i++)donated|=saved[i]!=0;
 for(int i=0;i<4;i++){
  int id=saved[i];if(id&&(id<11000000||id>=22000000||id%100!=i))return false;
  armor[i]=id?id:(!donated&&original?original[i]:11001100+i);
 }
 return true;
}
