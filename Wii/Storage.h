#pragma once
#include <string>
namespace Wii {
enum class StorageDevice { SD, USB };
inline const char *DeviceRoot(StorageDevice device) { return device==StorageDevice::SD?"sd:/":"usb:/"; }
inline const char *DeviceName(StorageDevice device) { return device==StorageDevice::SD?"SD card":"USB drive"; }
inline std::string DiscDirectory(StorageDevice device) { return std::string(DeviceRoot(device))+"disks/"; }
bool MountStorage(StorageDevice device);
bool StorageInserted(StorageDevice device);
bool StorageReadable(StorageDevice device);
void UnmountStorage();
}
