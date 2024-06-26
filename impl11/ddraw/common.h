// Copyright (c) 2014 J�r�my Ansel
// Licensed under the MIT license. See LICENSE.txt
// Extended for VR by Leo Reyes (c) 2019

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#ifndef STRICT
#define STRICT
#endif
#include <ddraw.h>
#include <d3d.h>

#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <Shlwapi.h>
#include <dwmapi.h>

#include <comdef.h>

#include <string>
#include <sstream>

#include "ComPtr.h"
#include "logger.h"
#include "utils.h"
#include "config.h"

// Needed to compile SSE2 instructions
#include <emmintrin.h>
