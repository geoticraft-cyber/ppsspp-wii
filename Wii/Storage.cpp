#include "Wii/Storage.h"
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include <dirent.h>
#include <ogc/libversion.h>
#ifndef _LIBOGC2_REVISION_
#error Storage requires libogc2.
#endif
#if _LIBOGC2_REVISION_ < 2487
#error Update to the reviewed libogc2 revision (see Wii/dependencies.lock.json).
#endif
namespace Wii {
static bool mounted[2]{};
bool StorageReadable(StorageDevice device) {
  DIR *dir=opendir(DeviceRoot(device));
  if(!dir) return false;
  closedir(dir);return true;
}
bool MountStorage(StorageDevice device) {
  if(StorageReadable(device)) return true;
  DISC_INTERFACE *interface=device==StorageDevice::SD?&__io_wiisd:&__io_usbstorage;
  const char *name=device==StorageDevice::SD?"sd":"usb";
  bool success=fatMountSimple(name,interface);
  mounted[device==StorageDevice::SD?0:1]|=success;
  return success&&StorageReadable(device);
}
bool StorageInserted(StorageDevice device) {
  DISC_INTERFACE *interface=device==StorageDevice::SD?&__io_wiisd:&__io_usbstorage;
  return interface->isInserted(interface);
}
void UnmountStorage() {
  if(mounted[1])fatUnmount("usb");
  if(mounted[0])fatUnmount("sd");
}
}
