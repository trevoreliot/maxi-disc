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
    // Analyser (Q3): LED segments, unlit and lit (brighter up the column), and the peak hold.
    Q_PROPERTY(QColor analyzerUnlit MEMBER m_analyzerUnlit CONSTANT FINAL)
    Q_PROPERTY(QColor analyzerLow MEMBER m_analyzerLow CONSTANT FINAL)
    Q_PROPERTY(QColor analyzerMid MEMBER m_analyzerMid CONSTANT FINAL)
    Q_PROPERTY(QColor analyzerHigh MEMBER m_analyzerHigh CONSTANT FINAL)
    Q_PROPERTY(QColor analyzerPeak MEMBER m_analyzerPeak CONSTANT FINAL)
    // The transport bar's MiniDisc (Q4): the bare disc, a printed mark that shows it turning,
    // its rim, and the metal hub (a gradient, a ring).
    Q_PROPERTY(QColor discBase MEMBER m_discBase CONSTANT FINAL)
    Q_PROPERTY(QColor discSheen MEMBER m_discSheen CONSTANT FINAL)
    Q_PROPERTY(QColor discRim MEMBER m_discRim CONSTANT FINAL)
    Q_PROPERTY(QColor discHub MEMBER m_discHub CONSTANT FINAL)
    Q_PROPERTY(QColor discHubHighlight MEMBER m_discHubHighlight CONSTANT FINAL)
    Q_PROPERTY(QColor discHubBorder MEMBER m_discHubBorder CONSTANT FINAL)
    // The MD Visualizer's pile (V1): MiniDisc cases seen from the spine. A case without art, the
    // ink on spines (dark or light, by contrast), the clear plastic at the case's ends, the light
    // and dark edges that make a spine look rounded, the shadow under the case slid out, and the
    // mix discs' paper label and pen.
    Q_PROPERTY(QColor spineDefault MEMBER m_spineDefault CONSTANT FINAL)
    Q_PROPERTY(QColor spineInkDark MEMBER m_spineInkDark CONSTANT FINAL)
    Q_PROPERTY(QColor spineInkLight MEMBER m_spineInkLight CONSTANT FINAL)
    Q_PROPERTY(QColor spinePlastic MEMBER m_spinePlastic CONSTANT FINAL)
    Q_PROPERTY(QColor spineEdgeLight MEMBER m_spineEdgeLight CONSTANT FINAL)
    Q_PROPERTY(QColor spineEdgeDark MEMBER m_spineEdgeDark CONSTANT FINAL)
    Q_PROPERTY(QColor spineShadow MEMBER m_spineShadow CONSTANT FINAL)
    Q_PROPERTY(QColor mdLabelPaper MEMBER m_mdLabelPaper CONSTANT FINAL)
    Q_PROPERTY(QColor mdLabelInk MEMBER m_mdLabelInk CONSTANT FINAL)
    // The case taken out (V2): its clear plastic sleeve (tint, edge, sheen), the shadow under it and
    // the shading that darkens a face as it turns away, the scrim under the front's caption, the back
    // insert, the clear cartridge shell, its metal shutter (light, mid, dark, the slot), and the
    // rules on a mix disc's paper.
    Q_PROPERTY(QColor mdCasePlastic MEMBER m_mdCasePlastic CONSTANT FINAL)
    Q_PROPERTY(QColor mdCaseBorder MEMBER m_mdCaseBorder CONSTANT FINAL)
    Q_PROPERTY(QColor mdCaseSheen MEMBER m_mdCaseSheen CONSTANT FINAL)
    Q_PROPERTY(QColor mdCaseShadow MEMBER m_mdCaseShadow CONSTANT FINAL)
    Q_PROPERTY(QColor mdFaceShade MEMBER m_mdFaceShade CONSTANT FINAL)
    Q_PROPERTY(QColor mdInsertScrim MEMBER m_mdInsertScrim CONSTANT FINAL)
    Q_PROPERTY(QColor mdBackInsertBg MEMBER m_mdBackInsertBg CONSTANT FINAL)
    Q_PROPERTY(QColor mdShellFill MEMBER m_mdShellFill CONSTANT FINAL)
    Q_PROPERTY(QColor mdShellBorder MEMBER m_mdShellBorder CONSTANT FINAL)
    Q_PROPERTY(QColor mdShutterLight MEMBER m_mdShutterLight CONSTANT FINAL)
    Q_PROPERTY(QColor mdShutterMid MEMBER m_mdShutterMid CONSTANT FINAL)
    Q_PROPERTY(QColor mdShutterDark MEMBER m_mdShutterDark CONSTANT FINAL)
    Q_PROPERTY(QColor mdShutterSlot MEMBER m_mdShutterSlot CONSTANT FINAL)
    Q_PROPERTY(QColor mdLabelRule MEMBER m_mdLabelRule CONSTANT FINAL)
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

    // A mix disc's case (V1): blank MDs came in coloured plastic; each playlist keeps one.
    Q_INVOKABLE QColor mdBlankColor(int id) const;

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
    QColor m_analyzerUnlit{0x1F, 0x22, 0x29};
    QColor m_analyzerLow{0xE0, 0x9F, 0x3E}; // = accent
    QColor m_analyzerMid{0xF2, 0xBC, 0x57};
    QColor m_analyzerHigh{0xFF, 0xE0, 0x8A};
    QColor m_analyzerPeak{0xFF, 0xF4, 0xCC};
    QColor m_discBase{0x2A, 0x2F, 0x39};
    QColor m_discSheen{0xFF, 0xFF, 0xFF, 0x26};
    QColor m_discRim{0x56, 0x5E, 0x70};
    QColor m_discHub{0x8C, 0x94, 0xA3};
    QColor m_discHubHighlight{0xD3, 0xD8, 0xE1};
    QColor m_discHubBorder{0x5A, 0x62, 0x72};
    QColor m_spineDefault{0x3B, 0x42, 0x52};
    QColor m_spineInkDark{0x15, 0x17, 0x1C};
    QColor m_spineInkLight{0xF1, 0xF2, 0xF4};
    QColor m_spinePlastic{0xDC, 0xE4, 0xF0, 0x59};
    QColor m_spineEdgeLight{0xFF, 0xFF, 0xFF, 0x40};
    QColor m_spineEdgeDark{0x00, 0x00, 0x00, 0x73};
    QColor m_spineShadow{0x00, 0x00, 0x00, 0x8C};
    QColor m_mdLabelPaper{0xEE, 0xEA, 0xDF};
    QColor m_mdLabelInk{0x1F, 0x2D, 0x52};
    QColor m_mdCasePlastic{0xC8, 0xD2, 0xE0, 0x30};
    QColor m_mdCaseBorder{0xD8, 0xE0, 0xEC, 0x8C};
    QColor m_mdCaseSheen{0xFF, 0xFF, 0xFF, 0x2E};
    QColor m_mdCaseShadow{0x00, 0x00, 0x00, 0x80};
    QColor m_mdFaceShade{0x05, 0x06, 0x09};
    QColor m_mdInsertScrim{0x0B, 0x0D, 0x12, 0xD9};
    QColor m_mdBackInsertBg{0x1A, 0x1D, 0x24};
    QColor m_mdShellFill{0xD6, 0xDE, 0xEA, 0x3A};
    QColor m_mdShellBorder{0xE4, 0xEA, 0xF2, 0xA6};
    QColor m_mdShutterLight{0xDC, 0xE0, 0xE6};
    QColor m_mdShutterMid{0xAA, 0xB0, 0xB9};
    QColor m_mdShutterDark{0x7C, 0x83, 0x8E};
    QColor m_mdShutterSlot{0x55, 0x5C, 0x67};
    QColor m_mdLabelRule{0x9F, 0xB6, 0xD6, 0x8C};
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
