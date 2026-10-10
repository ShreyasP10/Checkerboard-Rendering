#pragma once

// Compatibility shim: canonical limits now live in cbr/config.h so that a
// clean checkout builds even if this file is absent (it is NOT referenced by
// CMakeLists.txt or by any src/*.cpp). Kept only for backwards compatibility
// with older branches / CODEBASE.md snapshots that still do
// `#include "cbr/limits.h"`.
#include "cbr/config.h"
