// Copyright (c) 2026 Jeffrey Morley. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "ToolTipMeasure.h"

PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
	*data = new ToolTipMeasure(rm);
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
	auto* measure = static_cast<ToolTipMeasure*>(data);
	measure->Reload(rm);
	*maxValue = 1;
}

PLUGIN_EXPORT double Update(void* data)
{
	auto* measure = static_cast<ToolTipMeasure*>(data);
	return measure->Update();
}

PLUGIN_EXPORT LPCWSTR GetString(void* data)
{
	auto* measure = static_cast<ToolTipMeasure*>(data);
	return measure->GetString();
}

// This export identifies a DPI-aware plugin, preventing host pre-scaling of
// W, H and FontSize before the plugin applies the effective skin scale.
PLUGIN_EXPORT void HandleSkinSettingChange(void* data, void* rm, RmSkinSettingChange setting)
{
	if (setting == RmSkinSettingChange::Scale)
	{
		auto* measure = static_cast<ToolTipMeasure*>(data);
		// Scaling can invalidate the skin's current hover target.
		measure->Command(L"Hide");
		measure->Reload(rm);
	}
}

PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args)
{
	auto* measure = static_cast<ToolTipMeasure*>(data);
	measure->Command(args);
}

PLUGIN_EXPORT void Finalize(void* data)
{
	auto* measure = static_cast<ToolTipMeasure*>(data);
	delete measure;
}
