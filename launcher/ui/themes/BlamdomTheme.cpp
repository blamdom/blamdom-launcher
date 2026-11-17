// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Blamdom Launcher
 *  Copyright (C) 2026 Blamdom
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "BlamdomTheme.h"

namespace {
// Palette sampled from the Blamdom design reference (Dominic's mockup, Nov 2026):
// slate-navy surfaces with a vibrant blue primary action; green is reserved for the logo cube.
constexpr const char* kBg = "#0a192e";      // base: inputs, lists (darkest)
constexpr const char* kBg2 = "#0c1e3d";     // window
constexpr const char* kPanel = "#0f1f36";   // toolbar, status bar, menus
constexpr const char* kPanel2 = "#21395a";  // buttons, sidebar action rows
constexpr const char* kCard = "#132b50";    // welcome card fill
constexpr const char* kLine = "#1d385e";
constexpr const char* kLine2 = "#2e4a75";
constexpr const char* kText = "#ffffff";
constexpr const char* kMuted = "#a8bcd4";
constexpr const char* kDim = "#64748b";
constexpr const char* kAcc = "#3b82f6";     // primary blue
constexpr const char* kAccH = "#2563eb";    // hover (darker blue)
constexpr const char* kOnAcc = "#ffffff";
}  // namespace

QString BlamdomTheme::id()
{
    return "blamdom";
}

QString BlamdomTheme::name()
{
    return QObject::tr("Blamdom");
}

QString BlamdomTheme::tooltip()
{
    return QObject::tr("The Blamdom look: slate navy with blue accents");
}

QPalette BlamdomTheme::colorScheme()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(kBg2));
    p.setColor(QPalette::WindowText, QColor(kText));
    p.setColor(QPalette::Base, QColor(kBg));
    p.setColor(QPalette::AlternateBase, QColor("#0d2038"));
    p.setColor(QPalette::ToolTipBase, QColor(kPanel2));
    p.setColor(QPalette::ToolTipText, QColor(kText));
    p.setColor(QPalette::Text, QColor(kText));
    p.setColor(QPalette::Button, QColor(kPanel2));
    p.setColor(QPalette::ButtonText, QColor(kText));
    p.setColor(QPalette::BrightText, QColor("#ff6b5e"));
    p.setColor(QPalette::Link, QColor("#60a5fa"));
    p.setColor(QPalette::LinkVisited, QColor("#9a8cff"));
    p.setColor(QPalette::Highlight, QColor(kAcc));
    p.setColor(QPalette::HighlightedText, QColor(kOnAcc));
    p.setColor(QPalette::PlaceholderText, QColor(kDim));
    // bevel colors Fusion derives frames from
    p.setColor(QPalette::Light, QColor(kLine2));
    p.setColor(QPalette::Midlight, QColor(kLine));
    p.setColor(QPalette::Mid, QColor(kLine));
    p.setColor(QPalette::Dark, QColor("#081426"));
    p.setColor(QPalette::Shadow, QColor("#000000"));
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText }) {
        p.setColor(QPalette::Disabled, role, QColor(kDim));
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(kLine));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(kDim));
    return fadeInactive(p, fadeAmount(), fadeColor());
}

double BlamdomTheme::fadeAmount()
{
    return 0.4;
}

QColor BlamdomTheme::fadeColor()
{
    return QColor(kBg2);
}

bool BlamdomTheme::hasStyleSheet()
{
    return true;
}

QString BlamdomTheme::appStyleSheet()
{
    // Slate navy + blue primary, matching the Blamdom design reference.
    const QString css = QStringLiteral(R"(
QToolTip { background: %PANEL2%; color: %TEXT%; border: 1px solid %LINE2%; border-radius: 6px; padding: 4px 8px; }

QMainWindow, QDialog { background: %BG2%; }

QMenuBar { background: %PANEL%; border-bottom: 1px solid %LINE%; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 6px 12px; border-radius: 6px; }
QMenuBar::item:selected { background: %PANEL2%; }
QMenu { background: %PANEL%; border: 1px solid %LINE2%; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 14px; border-radius: 6px; }
QMenu::item:selected { background: %ACC%; color: %ONACC%; }
QMenu::item:disabled { color: %DIM%; background: transparent; }
QMenu::separator { height: 1px; background: %LINE%; margin: 6px 8px; }

QToolBar { background: %PANEL%; border: none; border-bottom: 1px solid %LINE%; spacing: 4px; padding: 4px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 5px 8px; }
QToolButton:hover { background: %PANEL2%; border-color: %LINE2%; }
QToolButton:pressed { background: #2a4a78; }
QToolButton:checked { background: %PANEL2%; border-color: %ACC%; }
QToolButton:disabled { color: %DIM%; }
QToolButton::menu-button { border: none; background: transparent; width: 16px; }
QToolButton[popupMode="1"] { padding-right: 18px; }
/* primary call-to-action (Add Instance): solid blue */
QToolButton[primary="true"] { background: %ACC%; color: %ONACC%; border: none; border-radius: 8px; padding: 6px 16px; font-weight: 600; }
QToolButton[primary="true"]:hover { background: %ACCH%; }
QToolButton[primary="true"]:pressed { background: #1d4ed8; }
/* the instance-name label sizes itself to its text; borders/padding would clip it */
EditableLabel QTextEdit { border: none; padding: 0; background: transparent; }

/* sidebar action rows: full width, icon left, rounded, spaced */
#instanceToolBar { padding: 6px; spacing: 8px; }
#instanceToolBar QToolButton { background: %PANEL2%; border: 1px solid %LINE2%; border-radius: 9px; padding: 7px 12px; min-height: 24px; text-align: left; }
#instanceToolBar QToolButton:hover { background: #27436b; border-color: %ACC%; }
#instanceToolBar QToolButton:pressed { background: #2a4a78; }
#instanceToolBar QToolButton:disabled { background: #14263f; border-color: %LINE%; color: %DIM%; }

QPushButton { background: %PANEL2%; border: 1px solid %LINE2%; border-radius: 8px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { border-color: %ACC%; }
QPushButton:pressed { background: #2a4a78; }
QPushButton:default { background: %ACC%; color: %ONACC%; border: none; }
QPushButton:default:hover { background: %ACCH%; }
QPushButton:disabled { background: %PANEL%; color: %DIM%; border-color: %LINE%; }

QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %BG%; border: 1px solid %LINE2%; border-radius: 8px; padding: 5px 8px;
    selection-background-color: %ACC%; selection-color: %ONACC%;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %ACC%; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %DIM%; background: %PANEL%; }
QComboBox QAbstractItemView { background: %PANEL%; border: 1px solid %LINE2%; selection-background-color: %ACC%; selection-color: %ONACC%; outline: 0; }

QAbstractItemView { background: %BG%; alternate-background-color: #0d2038; border: 1px solid %LINE%; border-radius: 8px;
    selection-background-color: %ACC%; selection-color: %ONACC%; outline: 0; }
QHeaderView::section { background: %PANEL%; color: %MUTED%; border: none; border-bottom: 1px solid %LINE%; padding: 6px 8px; font-weight: 600; }

QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid %LINE2%; background: %BG%; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACC%; }
QCheckBox::indicator:checked { background: %ACC%; border-color: %ACC%; }
QRadioButton::indicator:checked { border-color: %ACC%; background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 %ACC%, stop: 0.5 %ACC%, stop: 0.56 %BG%, stop: 1 %BG%); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: %LINE%; background: %PANEL%; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { border: none; width: 24px; }
QTabWidget::pane { border: 1px solid %LINE%; border-radius: 8px; top: -1px; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 8px 16px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: %TEXT%; }
QTabBar::tab:selected { color: %TEXT%; border-bottom: 2px solid %ACC%; }

QGroupBox { border: 1px solid %LINE%; border-radius: 10px; margin-top: 14px; padding: 14px 10px 10px 10px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; color: %MUTED%; font-weight: 600; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: %LINE2%; border-radius: 5px; min-height: 32px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #3d5f95; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %LINE2%; border-radius: 5px; min-width: 32px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #3d5f95; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background: %BG%; border: 1px solid %LINE2%; border-radius: 8px; text-align: center; min-height: 16px; }
QProgressBar::chunk { background: %ACC%; border-radius: 7px; }

QStatusBar { background: %PANEL%; border-top: 1px solid %LINE%; color: %MUTED%; }
QSplitter::handle { background: %LINE%; }
QLabel:disabled { color: %DIM%; }
)");
    QString out = css;
    const QList<QPair<QString, const char*>> subs = {
        { "%BG2%", kBg2 },     { "%BG%", kBg },     { "%PANEL2%", kPanel2 }, { "%PANEL%", kPanel }, { "%LINE2%", kLine2 }, { "%LINE%", kLine },
        { "%TEXT%", kText },   { "%MUTED%", kMuted }, { "%DIM%", kDim },    { "%ACC%", kAcc },     { "%ACCH%", kAccH },   { "%ONACC%", kOnAcc },
    };
    for (const auto& [key, value] : subs) {
        out.replace(key, QString::fromLatin1(value));
    }
    return out;
}
