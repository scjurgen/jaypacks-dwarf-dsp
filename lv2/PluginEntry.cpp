// The exported entry point of every plugin binary. Each plugin target compiles this file
// with its generated directory on the include path, so "ports.h" is that plugin's table
// (generator/jpgen.py); it defines jp::generated::PluginPorts.

#include "lv2/Lv2Adapter.h"
#include "ports.h"

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(const uint32_t index)
{
    return index == 0 ? jp::Lv2Adapter<jp::generated::PluginPorts>::descriptor() : nullptr;
}
