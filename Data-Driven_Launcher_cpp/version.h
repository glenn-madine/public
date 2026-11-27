// version.h -- the single place the DDLaunch+ and DDLaunch+ Editor version
// numbers, release dates and author live (B3). Included by DDLaunch.cpp,
// DDLaunchEdit.cpp, DDLaunch.rc and DDLaunchEdit.rc, so the About boxes, the
// window titles and the Explorer "Details" version can no longer disagree.
//
// To release a new version, change only this file (and add a "Changes in"
// note to the top of the .cpp file).
//
// Only plain #defines here: the resource compiler reads this file too.
// C++ code builds wide strings from these with literal concatenation,
// e.g. L"v" DDLAUNCH_VERSION_SHORT.

#ifndef DDLAUNCH_VERSION_H
#define DDLAUNCH_VERSION_H

#define DDLAUNCH_AUTHOR             "Glenn Madine"

// DDLaunch+ (DDLaunch.exe)
#define DDLAUNCH_VERSION_RC         2,7,1,0
#define DDLAUNCH_VERSION_STR        "2.7.1.0"
#define DDLAUNCH_VERSION_SHORT      "2.7.1"
#define DDLAUNCH_RELEASE_DATE       "11/26/2026"

// DDLaunch+ Editor (DDLaunchEdit.exe)
#define DDLAUNCHEDIT_VERSION_RC     1,0,2,0
#define DDLAUNCHEDIT_VERSION_STR    "1.0.2.0"
#define DDLAUNCHEDIT_VERSION_SHORT  "1.0.2"
#define DDLAUNCHEDIT_RELEASE_DATE   "11/26/2026"

#endif  // DDLAUNCH_VERSION_H
