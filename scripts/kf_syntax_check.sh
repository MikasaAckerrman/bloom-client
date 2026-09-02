#!/bin/sh
# Syntax-only check of cl_dll/death.cpp using the include paths and defines
# taken verbatim from cl_dll/CMakeLists.txt. No Android SDK needed; this only
# needs to parse and type-check, not link.
cd /tmp/minis-killfeed-bloom-20260822 || exit 1
g++ -fsyntax-only -Wall -Wextra -Wno-unknown-pragmas \
  -DCLIENT_WEAPONS -DCLIENT_DLL -DSTDINT_H='<cstdint>' \
  -DLINUX -D_LINUX \
  -Dstricmp=strcasecmp -D_strnicmp=strncasecmp -Dstrnicmp=strncasecmp \
  -fms-extensions \
  -I cl_dll/include \
  -I cl_dll/include/hud \
  -I cl_dll/include/studio \
  -I cl_dll/include/math \
  -I cl_dll \
  -I common \
  -I engine \
  -I pm_shared \
  -I dlls \
  -I game_shared \
  -I public \
  -I public/cl_dll \
  -I 3rdparty/mainui_cpp \
  -I 3rdparty/mainui_cpp/controls \
  -I 3rdparty/mainui_cpp/menus \
  -I 3rdparty/mainui_cpp/miniutl \
  -I cl_dll/particleman \
  "$@" 2>&1
echo "exit=$?"
