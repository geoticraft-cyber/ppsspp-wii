# Wii-only source bundle: the legacy multi-platform compiler backends are removed.
cmake_minimum_required(VERSION 3.6)
project(PPSSPP)
set(WII ON CACHE BOOL "Build the Nintendo Wii port")
include(Wii/Build.cmake)
