#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include "seeker-inputs.h"
static bool SeekerFile(const std::wstring& f){DWORD a=GetFileAttributesW(f.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
static bool ResolveSeekerAsset(const std::wstring& overlay,const std::wstring& base,const wchar_t* relative,std::wstring& result,bool& companion){
 result.clear();companion=false;
 if(!relative||!*relative||wcsstr(relative,L"..")||wcschr(relative,L':')||*relative==L'/'||*relative==L'\\')return false;
 std::wstring f=overlay+L"\\"+relative;if(SeekerFile(f)){result=f;companion=true;return true;}
 f=base+L"\\"+relative;if(SeekerFile(f)){result=f;return true;}return false;
}
static bool SeekerHash(const std::wstring& file,std::string& result){
 result.clear();HANDLE f=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
 DWORD length=0,returned=0;if(ok)ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&length,sizeof(length),&returned,0)>=0;
 std::vector<UCHAR> object(length),buffer(65536);UCHAR digest[32];if(ok)ok=BCryptCreateHash(alg,&hash,object.data(),length,nullptr,0,0)>=0;
 while(ok){DWORD count=0;if(!ReadFile(f,buffer.data(),(DWORD)buffer.size(),&count,nullptr)){ok=false;break;}if(!count)break;if(BCryptHashData(hash,buffer.data(),count,0)<0)ok=false;}
 if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);
 if(ok){const char* hex="0123456789abcdef";for(UCHAR b:digest){result+=hex[b>>4];result+=hex[b&15];}}return ok;
}
static bool ValidateSeekerInputs(const std::wstring& base,size_t& count){count=0;for(const auto& input:SeekerInputs){std::string actual;if(!SeekerHash(base+L"\\"+input.relative,actual)||actual!=input.sha256)return false;count++;}return true;}
static bool CreateVerifiedSeekerSave(const std::wstring& source,const std::wstring& backup,const std::wstring& destination){
 // CopyFile's fail-if-exists protects both pre-existing saves and backups, including races.
 if(SeekerFile(destination))return true;
 std::string a,b,c;if(!SeekerHash(source,a)||!CopyFileW(source.c_str(),backup.c_str(),TRUE)||!SeekerHash(backup,b)||a!=b)return false;
 if(!CopyFileW(backup.c_str(),destination.c_str(),TRUE))return false;
 if(!SeekerHash(destination,c)||b!=c){DeleteFileW(destination.c_str());return false;}return true;
}
