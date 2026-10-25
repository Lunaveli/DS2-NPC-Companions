#pragma once
// Scholar 1.0.3.0: morpheme animation-resource registry constructor.
// The native pool is 0x28 bytes of header plus capacity * (8-byte free-list
// pointer + 0x20-byte record). Keep all three constructor sizes consistent.
constexpr unsigned AnimationPoolCapacity=49152;
constexpr unsigned AnimationPoolBytes=0x28+AnimationPoolCapacity*40;
constexpr uintptr_t AnimationRegistryRva=0x1618950;
struct AnimationPoolSite {uintptr_t rva;unsigned size,immediate;unsigned char original[9];unsigned value;};
static const AnimationPoolSite AnimationPoolSites[]={
 {0x5f4ef2,5,1,{0xb9,0x28,0x80,0x07,0x00},AnimationPoolBytes},
 {0x5f4f38,6,2,{0x41,0xb8,0x00,0x30,0x00,0x00},AnimationPoolCapacity},
 {0x5f4f43,9,5,{0x48,0xc7,0x44,0x24,0x38,0x28,0x80,0x07,0x00},AnimationPoolBytes},
};
static bool AnimationPoolPatchState(uintptr_t base,bool& expanded){
 bool allOriginal=true,allExpanded=true;
 __try {for(const auto& s:AnimationPoolSites){unsigned char expected[9];memcpy(expected,s.original,s.size);memcpy(expected+s.immediate,&s.value,4);allOriginal&=memcmp((void*)(base+s.rva),s.original,s.size)==0;allExpanded&=memcmp((void*)(base+s.rva),expected,s.size)==0;}}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
 expanded=allExpanded;return allOriginal||allExpanded;
}
static bool ExpandAnimationPoolBeforeStartup(uintptr_t base){
 bool expanded=false;if(!AnimationPoolPatchState(base,expanded))return false;if(expanded)return true;
 DWORD protection[3]={};unsigned writable=0;
 for(unsigned i=0;i<3;i++){const auto& s=AnimationPoolSites[i];if(!VirtualProtect((void*)(base+s.rva),s.size,PAGE_EXECUTE_READWRITE,&protection[i]))break;writable++;}
 if(writable!=3){for(unsigned i=writable;i-->0;){DWORD ignored;VirtualProtect((void*)(base+AnimationPoolSites[i].rva),AnimationPoolSites[i].size,protection[i],&ignored);}return false;}
 for(const auto& s:AnimationPoolSites){memcpy((void*)(base+s.rva+s.immediate),&s.value,4);FlushInstructionCache(GetCurrentProcess(),(void*)(base+s.rva),s.size);}
 // Sites share a page: unwind in reverse so the original RX permission wins.
 for(unsigned i=3;i-->0;){DWORD ignored;VirtualProtect((void*)(base+AnimationPoolSites[i].rva),AnimationPoolSites[i].size,protection[i],&ignored);}
 return AnimationPoolPatchState(base,expanded)&&expanded;
}
static bool ReadAnimationPoolUsage(uintptr_t base,int& capacity,int& available){
 __try {uintptr_t registry=*(uintptr_t*)(base+AnimationRegistryRva);if(!registry)return false;uintptr_t pool=*(uintptr_t*)(registry+0x18);if(!pool)return false;capacity=*(int*)(pool+0x10);available=*(int*)(pool+0x14);return capacity>0&&capacity<=1000000&&available>=0&&available<=capacity;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
