#include "Wii/Storage.h"
#include <fat.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
extern DISC_INTERFACE __io_wiisd,__io_usbstorage;
static bool sdPresent=true,usbPresent=false;
static int mountCalls=0,unmountCalls=0;
static bool inserted(DISC_INTERFACE *d) { assert(d==&__io_wiisd||d==&__io_usbstorage);return d==&__io_wiisd?sdPresent:usbPresent; }
DISC_INTERFACE __io_wiisd={inserted},__io_usbstorage={inserted};
bool fatMountSimple(const char *name,DISC_INTERFACE *d) {
 ++mountCalls;
 assert((strcmp(name,"sd")==0&&d==&__io_wiisd)||(strcmp(name,"usb")==0&&d==&__io_usbstorage));
 if(!inserted(d))return false;
 return mkdir((std::string(name)+":").c_str(),0755)==0;
}
bool fatUnmount(const char *name) {++unmountCalls;return rmdir((std::string(name)+":").c_str())==0;}
int main() {
 char temp[]="storage-test-XXXXXX";char *root=mkdtemp(temp);assert(root);assert(chdir(root)==0);
 using namespace Wii;
 assert(std::string(DeviceRoot(StorageDevice::SD))=="sd:/");
 assert(DiscDirectory(StorageDevice::USB)=="usb:/disks/");
 assert(!MountStorage(StorageDevice::USB)&&mountCalls==1);
 assert(MountStorage(StorageDevice::SD)&&StorageReadable(StorageDevice::SD));
 assert(MountStorage(StorageDevice::SD)&&mountCalls==2); // no duplicate mount
 usbPresent=true;
 assert(MountStorage(StorageDevice::USB)&&mountCalls==3);
 assert(StorageInserted(StorageDevice::USB)&&StorageInserted(StorageDevice::SD));
 UnmountStorage();assert(unmountCalls==2);
 assert(chdir("..")==0);assert(rmdir(root)==0);
 puts("PASS: SD/USB interface routing, unavailable USB, retry, mount reuse and unmount (mock I/O)");
}
