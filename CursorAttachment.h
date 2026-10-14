// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once
#include "vendor/StdAfx.h"

namespace ToolTip {
struct CursorEdges
{
	POINT top{}, bottom{};
	RECT bounds{};
};

CursorEdges GetCursorEdges(HCURSOR cursor);

}
