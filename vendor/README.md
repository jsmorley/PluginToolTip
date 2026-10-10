# Bundled dependencies

The Gfx text formatter, font collection, DirectWrite helper and font loader files were copied from
`C:\Rainmeter4\Common\Gfx` on 2026-10-10. They are Rainmeter Team code licensed under GNU GPL v2;
the complete license is in the project `LICENSE`. The formatter includes Rainmeter's inline
color, gradient, typography, spacing, case and shadow implementations.

`Gfx/Canvas.h` is a small ToolTip-specific adapter. The corresponding drawing adapter is
`../ToolTipRenderer.cpp`. `StdAfx.h` supplies platform includes. `StringUtil.h` contains only
the case helpers extracted from Rainmeter's Common/StringUtil.cpp. FontCollection.cpp has
a lifetime fix for the custom COM font collection. No dependency on that local source tree remains.

`pcre` is the PCRE 8.45 UTF-16 implementation from `C:\Rainmeter4\ThirdParty\pcre`,
with its original license at `pcre/LICENCE`. It is built directly into the plugin. Only the
`pcre16_*.c` wrappers selected in the project are compiled, rather than their included sources
being compiled a second time. Pattern execution is bounded to protect the skin update loop.
