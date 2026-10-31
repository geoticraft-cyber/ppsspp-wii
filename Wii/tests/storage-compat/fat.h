#pragma once
struct DISC_INTERFACE { bool (*isInserted)(DISC_INTERFACE*); };
bool fatMountSimple(const char *,DISC_INTERFACE *);
bool fatUnmount(const char *);
