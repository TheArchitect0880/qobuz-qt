#pragma once

#include <QColor>

namespace Colors {

// Brand accents
inline const QColor QobuzOrange{0xFF, 0xB2, 0x32};
inline const QColor QobuzBlue  {0x46, 0xB3, 0xEE};

// Badge / indicator colors used in tree-view item foregrounds
inline const QColor BadgeGreen {QStringLiteral("#2FA84F")};
inline const QColor BadgeBlue  {QStringLiteral("#2B7CD3")};
inline const QColor BadgeGray  {QStringLiteral("#8E8E93")};

// Text
inline const QColor LightText      {0xe8, 0xe8, 0xe8};
inline const QColor SubduedText    {0xaa, 0xaa, 0xaa};
inline const QColor PlaceholderText{0x66, 0x66, 0x66};
inline const QColor DisabledText   {0x55, 0x55, 0x55};

// Surfaces / backgrounds
inline const QColor WindowBg       {0x19, 0x19, 0x19};
inline const QColor BaseBg         {0x14, 0x14, 0x14};
inline const QColor AlternateBaseBg{0x1e, 0x1e, 0x1e};
inline const QColor ButtonSurface  {0x2a, 0x2a, 0x2a};
inline const QColor ContextBg      {0x1a, 0x1a, 0x1a};
inline const QColor MidSurface     {0x2f, 0x2f, 0x2f};
inline const QColor DarkSurface    {0x0e, 0x0e, 0x0e};
inline const QColor HighlightedFg  {0x10, 0x10, 0x10};

} // namespace Colors
