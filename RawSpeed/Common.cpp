#include "StdAfx.h"
#include "Common.h"
/* 
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

    http://www.klauspost.com
*/


#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>

int macosx_version()
{
  SInt32 gestalt_version;
  static int ver = 0; // cached
  if (0 == ver && (Gestalt(gestaltSystemVersion, &gestalt_version) == noErr)) {
    ver = gestalt_version;
  }
  return ver;
}
void* _aligned_malloc(size_t bytes, size_t alignment) {

  if (macosx_version() >=0x1060) { // 10.6+
    void* ret= NULL;
    if (0 == posix_memalign(&ret, alignment, bytes))
      return ret;
    else
      return NULL;
  } 
  return malloc(bytes); // Mac OS X malloc is usually aligned to 16 bytes
}

#elif defined(__unix__)

void* _aligned_malloc(size_t bytes, size_t alignment) {
  void* ret= NULL;
  if (0 == posix_memalign(&ret, alignment, bytes))
    return ret;
  else
    return NULL;
}

#endif

namespace RawSpeed {

void writeLog(int priority, const char *format, ...)
{
  string msg("RawSpeed:");
  msg.append(format);
  va_list args;
  va_start(args, format);

#ifdef _DEBUG
  vprintf(msg.c_str(), args);
#else
  if(priority < DEBUG_PRIO_INFO)
    vprintf(msg.c_str(), args);
#endif // _DEBUG
  va_end(args);
}

} // Namespace RawSpeed