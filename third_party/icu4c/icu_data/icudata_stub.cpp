// ICU static-data link anchor for the P6 MUI text stack.
//
// UnicodeTextEngine loads the vendored icudt_godot.dat at startup and calls
// udata_setCommonData before any BiDi or break-iterator use. ICU still
// references this symbol from udata.cpp in a static build, so retain a tiny
// anchor rather than embedding the 4.8 MiB data blob into every executable.

#include <cstddef>

#include <unicode/udata.h>
#include <unicode/utypes.h>

extern "C" U_EXPORT const size_t U_ICUDATA_SIZE = 0;
extern "C" U_EXPORT const unsigned char U_ICUDATA_ENTRY_POINT[] = {0};
