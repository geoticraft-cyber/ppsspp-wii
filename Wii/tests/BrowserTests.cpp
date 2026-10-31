#include "Wii/DiscBrowser.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <algorithm>
static void touch(const std::string &p) {FILE *f=fopen(p.c_str(),"wb");assert(f);fclose(f);}
int main() {
 char pattern[]="./wii-browser-XXXXXX";char *dir=mkdtemp(pattern);assert(dir);
 const std::string root=dir;std::string error;Wii::ScanReport report;
 mkdir((root+"/nested").c_str(),0755);mkdir((root+"/folder.iso").c_str(),0755);
 touch(root+"/UPPER.ISO");touch(root+"/nested/Space Name.CsO");touch(root+"/ignore.iso.zip");
 assert(symlink("no-such-file",(root+"/unreadable.iso").c_str())==0);
 auto games=Wii::ScanDiscs(root,&error,&report);
 assert(error.empty()&&report.folderReadable&&games.size()==3&&report.candidates==3);
 assert(report.metadataFailures==1&&report.folders==3);
 bool nested=false,unreadable=false;
 for(const auto &g:games) {nested|=g.path==root+"/nested/Space Name.CsO";unreadable|=g.name=="unreadable.iso";}
 assert(nested&&unreadable); // metadata failures and zero-size files stay visible
 assert(Wii::ScanDiscs(root,&error,&report,false).size()==2);
 assert(!Wii::ProbeDisc(root+"/unreadable.iso",&error)&&!error.empty());
 assert(Wii::ScanDiscs(root+"/absent",&error,&report).empty()&&!error.empty()&&!report.folderReadable);
 assert(Wii::ScanDiscs(root+"/folder.iso",&error,&report).empty()&&error.empty()&&report.folderReadable&&report.entries==0);
 assert(Wii::DirectorySummary(root).find("UPPER.ISO")!=std::string::npos);
 // Preserve the previous ASCII-folded ordering, including same-name path ties,
 // long names and non-ASCII bytes, without lowercase string allocations.
 std::vector<std::string> names={"a.ISO","A.iso","alpha.iso","ALPHA.CSO",
   "z.iso",std::string(180,'q')+".iso",std::string("\xc3\xa9")+".cso"};
 for(const auto &name:names)touch(root+"/"+name);
 touch(root+"/nested/a.ISO");
 games=Wii::ScanDiscs(root,&error,&report);
 auto expected=games;
 const auto fold=[](std::string s){for(char &c:s)if(c>='A'&&c<='Z')c+='a'-'A';return s;};
 std::sort(expected.begin(),expected.end(),[&](const Wii::DiscImage &a,const Wii::DiscImage &b){
   const auto x=fold(a.name),y=fold(b.name);return x!=y?x<y:a.path<b.path;
 });
 for(size_t i=0;i<games.size();++i)assert(games[i].path==expected[i].path);
 for(const auto &name:names){assert(Wii::IsDiscFilename(name));unlink((root+"/"+name).c_str());}
 unlink((root+"/nested/a.ISO").c_str());
 for(const char *f:{"UPPER.ISO","ignore.iso.zip","unreadable.iso","nested/Space Name.CsO"})unlink((root+"/"+f).c_str());
 rmdir((root+"/nested").c_str());rmdir((root+"/folder.iso").c_str());rmdir(root.c_str());
 puts("PASS: nested/mixed-case discs, zero size, metadata failure, missing vs empty folder, root diagnostics");
}
