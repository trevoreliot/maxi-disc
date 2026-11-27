// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <array>

Theme::Theme(QObject *parent)
    : QObject(parent)
{
}

QColor Theme::mdBlankColor(int id) const
{
    // Blue, red, green, smoke, violet, orange, clear: as seen lying in a pile, so opaque.
    static const std::array<QColor, 7> kBlanks = {
        QColor(0x3A, 0x68, 0xB0), QColor(0xB5, 0x40, 0x3E), QColor(0x3C, 0x8A, 0x5C), QColor(0x4D, 0x52, 0x5E),
        QColor(0x6C, 0x52, 0xA3), QColor(0xC4, 0x70, 0x2E), QColor(0x9C, 0xA8, 0xB8),
    };
    const auto n = static_cast<int>(kBlanks.size());
    return kBlanks[static_cast<size_t>(((id % n) + n) % n)];
}
