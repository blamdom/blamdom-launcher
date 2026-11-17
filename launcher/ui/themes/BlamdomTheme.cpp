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
// mine.blamdom.dev palette
constexpr const char* kBg = "#122b4a";      // inputs, lists (darkest)
constexpr const char* kBg2 = "#17395e";     // window (the Blamdom navy)
constexpr const char* kPanel = "#1c4170";   // raised surfaces
constexpr const char* kPanel2 = "#214a80";  // buttons
constexpr const char* kLine = "#2e5c94";
constexpr const char* kLine2 = "#3a6cab";
constexpr const char* kText = "#eef3fc";
constexpr const char* kMuted = "#b6c4da";
constexpr const char* kDim = "#8b9cb6";
constexpr const char* kGrass = "#53e26b";
constexpr const char* kOnGrass = "#07130a";
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
    return QObject::tr("The Blamdom look: navy with grass-green accents");
}

QPalette BlamdomTheme::colorScheme()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(kBg2));
    p.setColor(QPalette::WindowText, QColor(kText));
    p.setColor(QPalette::Base, QColor(kBg));
    p.setColor(QPalette::AlternateBase, QColor("#15325c"));
    p.setColor(QPalette::ToolTipBase, QColor(kPanel2));
    p.setColor(QPalette::ToolTipText, QColor(kText));
    p.setColor(QPalette::Text, QColor(kText));
    p.setColor(QPalette::Button, QColor(kPanel2));
    p.setColor(QPalette::ButtonText, QColor(kText));
    p.setColor(QPalette::BrightText, QColor("#ff6b5e"));
    p.setColor(QPalette::Link, QColor("#4d9fff"));
    p.setColor(QPalette::LinkVisited, QColor("#9a8cff"));
    p.setColor(QPalette::Highlight, QColor(kGrass));
    p.setColor(QPalette::HighlightedText, QColor(kOnGrass));
    p.setColor(QPalette::PlaceholderText, QColor(kDim));
    // bevel colors Fusion derives frames from
    p.setColor(QPalette::Light, QColor(kLine2));
    p.setColor(QPalette::Midlight, QColor(kLine));
    p.setColor(QPalette::Mid, QColor(kLine));
    p.setColor(QPalette::Dark, QColor("#0d2140"));
    p.setColor(QPalette::Shadow, QColor("#000000"));
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText }) {
        p.setColor(QPalette::Disabled, role, QColor("#7f92ad"));
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(kLine2));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#7f92ad"));
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
    // Deliberately conservative: rounded, flat, thin borders. Checkboxes, radios, sliders and arrows are left to
    // Fusion (they follow the palette), because restyling them with QSS needs image assets.
    const QString css = QStringLiteral(R"(
QToolTip { background: %PANEL2%; color: %TEXT%; border: 1px solid %LINE2%; border-radius: 6px; padding: 4px 8px; }

QMainWindow, QDialog { background: %BG2%; }

QMenuBar { background: %BG%; border-bottom: 1px solid %LINE%; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 6px 12px; border-radius: 6px; }
QMenuBar::item:selected { background: %PANEL2%; }
QMenu { background: %PANEL%; border: 1px solid %LINE2%; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 14px; border-radius: 6px; }
QMenu::item:selected { background: %GRASS%; color: %ONGRASS%; }
QMenu::item:disabled { color: %DIM%; background: transparent; }
QMenu::separator { height: 1px; background: %LINE%; margin: 6px 8px; }

QToolBar { background: %BG%; border: none; border-bottom: 1px solid %LINE%; spacing: 4px; padding: 4px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 5px 8px; }
QToolButton:hover { background: %PANEL2%; border-color: %LINE2%; }
QToolButton:pressed, QToolButton:checked { background: #274f85; border-color: %GRASS%; }
QToolButton:disabled { color: %DIM%; }
QToolButton::menu-button { border: none; background: transparent; width: 16px; }
QToolButton[popupMode="1"] { padding-right: 18px; }
/* the instance-name label sizes itself to its text; borders/padding would clip it */
EditableLabel QTextEdit { border: none; padding: 0; background: transparent; }

QPushButton { background: %PANEL2%; border: 1px solid %LINE2%; border-radius: 8px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { border-color: %GRASS%; }
QPushButton:pressed { background: #274f85; }
QPushButton:default { background: %GRASS%; color: %ONGRASS%; border-color: %GRASS%; }
QPushButton:default:hover { background: #6bec81; }
QPushButton:disabled { background: %PANEL%; color: %DIM%; border-color: %LINE%; }

QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %BG%; border: 1px solid %LINE2%; border-radius: 8px; padding: 5px 8px;
    selection-background-color: %GRASS%; selection-color: %ONGRASS%;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %GRASS%; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %DIM%; background: %PANEL%; }
QComboBox QAbstractItemView { background: %PANEL%; border: 1px solid %LINE2%; selection-background-color: %GRASS%; selection-color: %ONGRASS%; outline: 0; }

QAbstractItemView { background: %BG%; alternate-background-color: #15325c; border: 1px solid %LINE%; border-radius: 8px;
    selection-background-color: %GRASS%; selection-color: %ONGRASS%; outline: 0; }
QHeaderView::section { background: %PANEL%; color: %MUTED%; border: none; border-bottom: 1px solid %LINE%; padding: 6px 8px; font-weight: 600; }

QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid %LINE2%; background: %BG%; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %GRASS%; }
QCheckBox::indicator:checked { background: %GRASS%; border-color: %GRASS%; }
QRadioButton::indicator:checked { border-color: %GRASS%; background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 %GRASS%, stop: 0.5 %GRASS%, stop: 0.56 %BG%, stop: 1 %BG%); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: %LINE%; background: %PANEL%; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { border: none; width: 24px; }
QTabWidget::pane { border: 1px solid %LINE%; border-radius: 8px; top: -1px; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 8px 16px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: %TEXT%; }
QTabBar::tab:selected { color: %TEXT%; border-bottom: 2px solid %GRASS%; }

QGroupBox { border: 1px solid %LINE%; border-radius: 10px; margin-top: 14px; padding: 14px 10px 10px 10px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; color: %MUTED%; font-weight: 600; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: %LINE2%; border-radius: 5px; min-height: 32px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #4a78b0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %LINE2%; border-radius: 5px; min-width: 32px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #4a78b0; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background: %BG%; border: 1px solid %LINE2%; border-radius: 8px; text-align: center; min-height: 16px; }
QProgressBar::chunk { background: %GRASS%; border-radius: 7px; }

QStatusBar { background: %BG%; border-top: 1px solid %LINE%; color: %MUTED%; }
QSplitter::handle { background: %LINE%; }
QLabel:disabled { color: %DIM%; }
)");
    QString out = css;
    const QList<QPair<QString, const char*>> subs = {
        { "%BG2%", kBg2 },       { "%BG%", kBg },     { "%PANEL2%", kPanel2 }, { "%PANEL%", kPanel }, { "%LINE2%", kLine2 }, { "%LINE%", kLine },
        { "%TEXT%", kText },     { "%MUTED%", kMuted }, { "%DIM%", kDim },     { "%GRASS%", kGrass }, { "%ONGRASS%", kOnGrass },
    };
    for (const auto& [key, value] : subs) {
        out.replace(key, QString::fromLatin1(value));
    }
    return out;
}
