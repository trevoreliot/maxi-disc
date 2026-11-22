// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// Design tokens (QML singleton `Theme`): colours, spacing, radii, font sizes.
// Dark slate with an MD-deck amber accent. Main.qml maps these onto the window palette so
// the Fusion controls (menus, scrollbars, sliders) match.
// Fixed for now; a user-configurable theme is a follow-up (docs/PLAN.md).
class Theme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Surfaces
    Q_PROPERTY(QColor bg MEMBER m_bg CONSTANT FINAL)
    Q_PROPERTY(QColor surface MEMBER m_surface CONSTANT FINAL)
    Q_PROPERTY(QColor surfaceElevated MEMBER m_surfaceElevated CONSTANT FINAL)
    Q_PROPERTY(QColor surfaceChrome MEMBER m_surfaceChrome CONSTANT FINAL)
    Q_PROPERTY(QColor surfaceInset MEMBER m_surfaceInset CONSTANT FINAL)
    // Borders
    Q_PROPERTY(QColor border MEMBER m_border CONSTANT FINAL)
    Q_PROPERTY(QColor borderSubtle MEMBER m_borderSubtle CONSTANT FINAL)
    Q_PROPERTY(QColor borderStrong MEMBER m_borderStrong CONSTANT FINAL)
    // Text
    Q_PROPERTY(QColor textPrimary MEMBER m_textPrimary CONSTANT FINAL)
    Q_PROPERTY(QColor textSecondary MEMBER m_textSecondary CONSTANT FINAL)
    Q_PROPERTY(QColor textMuted MEMBER m_textMuted CONSTANT FINAL)
    // Accent and keyboard focus
    Q_PROPERTY(QColor accent MEMBER m_accent CONSTANT FINAL)
    Q_PROPERTY(QColor accentMuted MEMBER m_accentMuted CONSTANT FINAL)
    Q_PROPERTY(QColor focusRing MEMBER m_focusRing CONSTANT FINAL)
    Q_PROPERTY(QColor onAccent MEMBER m_onAccent CONSTANT FINAL)
    // List rows: hover, keyboard cursor in an unfocused pane, cursor in the focused pane,
    // rows picked for a multi-track action (tag editing)
    Q_PROPERTY(QColor rowHover MEMBER m_rowHover CONSTANT FINAL)
    Q_PROPERTY(QColor rowMarked MEMBER m_rowMarked CONSTANT FINAL)
    Q_PROPERTY(QColor rowSelected MEMBER m_rowSelected CONSTANT FINAL)
    Q_PROPERTY(QColor rowActiveFocused MEMBER m_rowActiveFocused CONSTANT FINAL)
    // Banners
    Q_PROPERTY(QColor errorBg MEMBER m_errorBg CONSTANT FINAL)
    Q_PROPERTY(QColor errorText MEMBER m_errorText CONSTANT FINAL)
    Q_PROPERTY(QColor infoBg MEMBER m_infoBg CONSTANT FINAL)
    Q_PROPERTY(QColor infoText MEMBER m_infoText CONSTANT FINAL)
    Q_PROPERTY(double watermarkOpacity MEMBER m_watermarkOpacity CONSTANT FINAL)
    // Spacing
    Q_PROPERTY(int spaceXS MEMBER m_spaceXS CONSTANT FINAL)
    Q_PROPERTY(int spaceSM MEMBER m_spaceSM CONSTANT FINAL)
    Q_PROPERTY(int spaceMD MEMBER m_spaceMD CONSTANT FINAL)
    Q_PROPERTY(int spaceLG MEMBER m_spaceLG CONSTANT FINAL)
    Q_PROPERTY(int spaceXL MEMBER m_spaceXL CONSTANT FINAL)
    Q_PROPERTY(int space2XL MEMBER m_space2XL CONSTANT FINAL)
    // Radii
    Q_PROPERTY(int radiusSmall MEMBER m_radiusSmall CONSTANT FINAL)
    Q_PROPERTY(int radiusMedium MEMBER m_radiusMedium CONSTANT FINAL)
    Q_PROPERTY(int radiusLarge MEMBER m_radiusLarge CONSTANT FINAL)
    // Font pixel sizes
    Q_PROPERTY(int fontDisplay MEMBER m_fontDisplay CONSTANT FINAL)
    Q_PROPERTY(int fontTitle MEMBER m_fontTitle CONSTANT FINAL)
    Q_PROPERTY(int fontHeading MEMBER m_fontHeading CONSTANT FINAL)
    Q_PROPERTY(int fontBody MEMBER m_fontBody CONSTANT FINAL)
    Q_PROPERTY(int fontSmall MEMBER m_fontSmall CONSTANT FINAL)
    Q_PROPERTY(int fontMicro MEMBER m_fontMicro CONSTANT FINAL)
    Q_PROPERTY(QString fontMono MEMBER m_fontMono CONSTANT FINAL)

public:
    explicit Theme(QObject *parent = nullptr);

private:
    QColor m_bg{0x16, 0x18, 0x1D};
    QColor m_surface{0x1E, 0x21, 0x28};
    QColor m_surfaceElevated{0x26, 0x2A, 0x33};
    QColor m_surfaceChrome{0x1B, 0x1E, 0x24};
    QColor m_surfaceInset{0x11, 0x13, 0x17};
    QColor m_border{0x32, 0x37, 0x44};
    QColor m_borderSubtle{0x26, 0x2A, 0x34};
    QColor m_borderStrong{0x4A, 0x51, 0x64};
    QColor m_textPrimary{0xEA, 0xEC, 0xEF};
    QColor m_textSecondary{0xA1, 0xA8, 0xB8};
    QColor m_textMuted{0x6E, 0x75, 0x85};
    QColor m_accent{0xE0, 0x9F, 0x3E};
    QColor m_accentMuted{0x6B, 0x53, 0x2B};
    QColor m_focusRing{0xF2, 0xB8, 0x57};
    QColor m_onAccent{0x16, 0x18, 0x1D};
    QColor m_rowHover{0xFF, 0xFF, 0xFF, 0x1A};
    QColor m_rowMarked{0xE0, 0x9F, 0x3E, 0x1C};
    QColor m_rowSelected{0xE0, 0x9F, 0x3E, 0x29};
    QColor m_rowActiveFocused{0xE0, 0x9F, 0x3E, 0x40};
    QColor m_errorBg{0x4A, 0x1F, 0x24};
    QColor m_errorText{0xFF, 0xD6, 0xD9};
    QColor m_infoBg{0x1F, 0x32, 0x42};
    QColor m_infoText{0xD2, 0xE9, 0xFF};
    double m_watermarkOpacity = 0.10;
    int m_spaceXS = 4;
    int m_spaceSM = 8;
    int m_spaceMD = 12;
    int m_spaceLG = 16;
    int m_spaceXL = 24;
    int m_space2XL = 32;
    int m_radiusSmall = 4;
    int m_radiusMedium = 6;
    int m_radiusLarge = 10;
    int m_fontDisplay = 22;
    int m_fontTitle = 18;
    int m_fontHeading = 15;
    int m_fontBody = 13;
    int m_fontSmall = 11;
    int m_fontMicro = 10;
    QString m_fontMono = QStringLiteral("monospace");
};
