#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include <afxwin.h>
#include <afxext.h>
#include <afxdlgs.h>
#include <afxcmn.h>
#include <afxcontrolbars.h>
#include <afxpropertygridctrl.h>

#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>

// GDI+ needs the min / max macros of windows.h; the code of the plugin uses std::min / std::max
#undef min
#undef max

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>
