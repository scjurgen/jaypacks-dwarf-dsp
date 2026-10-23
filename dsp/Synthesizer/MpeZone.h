#pragma once

#include <algorithm>

namespace jp
{

/**
 * @brief The MIDI channels of an MPE zone: a master channel and a range of member channels; every other channel
 * is outside.
 *
 * Channels are 0 to 15 on the wire and 1 to 16 where a person sets them (the setters take 1 to 16 and clamp);
 * accepts() takes the wire value (the low nibble of the status byte). The default is the lower zone of the MPE
 * specification: master 1, members 2 to 16. A member range with lower above upper is empty, so only the master
 * channel is inside.
 */
class MpeZone
{
  public:
    void setMasterChannel(const int channel) noexcept
    {
        m_master = toWire(channel);
    }

    void setLowerMemberChannel(const int channel) noexcept
    {
        m_lower = toWire(channel);
    }

    void setUpperMemberChannel(const int channel) noexcept
    {
        m_upper = toWire(channel);
    }

    [[nodiscard]] bool accepts(const int channel) const noexcept
    {
        return channel == m_master || (channel >= m_lower && channel <= m_upper);
    }

    [[nodiscard]] int masterChannel() const noexcept
    {
        return m_master + 1;
    }

    [[nodiscard]] int lowerMemberChannel() const noexcept
    {
        return m_lower + 1;
    }

    [[nodiscard]] int upperMemberChannel() const noexcept
    {
        return m_upper + 1;
    }

  private:
    [[nodiscard]] static int toWire(const int channel) noexcept
    {
        return std::clamp(channel - 1, 0, 15);
    }

    int m_master{0};
    int m_lower{1};
    int m_upper{15};
};

}
