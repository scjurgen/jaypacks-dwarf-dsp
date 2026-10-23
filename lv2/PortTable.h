#pragma once

#include <cstdint>

namespace jp
{

/// One control input port of a plugin, as the generator writes it into ports.h: LV2 port
/// index, symbol, range and default from the JSON, and the pedal setter that receives the
/// value (in port units, unconverted).
template <typename Pedal>
struct ControlInput
{
    uint32_t index;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    void (Pedal::*setter)(float);
};

/// One control output port: LV2 port index, symbol and the pedal getter whose value the
/// adapter writes to the port after every run().
template <typename Pedal>
struct ControlOutput
{
    uint32_t index;
    const char* symbol;
    float (Pedal::*getter)() const;
};

}
