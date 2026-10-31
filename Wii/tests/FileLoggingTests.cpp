// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/FileLogging.h"
#include "Wii/Diagnostics.h"
#include "Wii/AllocationDiagnostics.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
static bool Exists(const std::string &p){struct stat st;return stat(p.c_str(),&st)==0;}
static std::string Read(const std::string &p){FILE *f=fopen(p.c_str(),"rb");assert(f);std::string s;char b[1024];while(size_t n=fread(b,1,sizeof(b),f))s.append(b,n);fclose(f);return s;}
int main(int argc,char **argv){
 assert(argc==2);const std::string root=std::string(argv[1])+"/";FILE *out=stdout,*err=stderr;
 assert(Wii::SetFileLogging(root,false,3));assert(!Wii::DiagnosticLevel());
 for(unsigned n=0;n<10000;++n){Wii::TraceEvent(Wii::TraceKind::IOBegin,n);Wii::DiagnosticMessage(2,"test","disabled",0,0);Wii::DiagnosticText("test","disabled");}
 Wii::AllocationFailure("disabled",1,0,0,0,0);assert(Wii::AllocationFailureCount()==1);
 for(const char *n:{"ppsspp.log","errors.log","console.log","allocation-failures.log","ppsspp.log.previous"})assert(!Exists(root+n));
 assert(Wii::SetFileLogging(root,true,1));assert(stdout!=out&&stderr!=err&&Wii::DiagnosticLevel()==1);
 printf("console marker one\n");fprintf(stderr,"stderr marker one\n");
 Wii::TraceEvent(Wii::TraceKind::IOBegin,0x08800000);Wii::DiagnosticMessage(2,"test","enabled",0,0);
 Wii::AllocationFailure("enabled",2,0,0,0,0);
 assert(Wii::SetFileLogging(root,false,3));assert(stdout==out&&stderr==err&&!Wii::DiagnosticLevel());
 const std::string prior=Read(root+"ppsspp.log");assert(prior.find("enabled")!=std::string::npos);
 assert(Read(root+"console.log").find("console marker one")!=std::string::npos);
 assert(Read(root+"errors.log").find("stderr marker one")!=std::string::npos);
 assert(Read(root+"allocation-failures.log").find("kind=enabled")!=std::string::npos);
 assert(Wii::SetFileLogging(root,false,1));Wii::DiagnosticMessage(2,"test","disabled again",0,0);
 assert(Read(root+"ppsspp.log")==prior&&!Exists(root+"ppsspp.log.previous"));
 for(unsigned n=0;n<8;++n){assert(Wii::SetFileLogging(root,true,2));printf("console marker two\n");Wii::DiagnosticText("test","reenabled");Wii::CloseFileLogging();assert(stdout==out&&stderr==err);}
 assert(Read(root+"ppsspp.log").find(prior)==0);assert(Read(root+"console.log").find("console marker two")!=std::string::npos);
 assert(!Wii::SetFileLogging(root+"missing/",true,1));assert(stdout==out&&stderr==err&&!Wii::DiagnosticLevel());
 Wii::CloseFileLogging();puts("pass: logging off creates/writes no logs; stream restoration, allocation output, on/off persistence boundary, repeated append/reopen, failed open cleanup");
}
