// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "InstanceView.h"

#include <QAccessible>
#include <QApplication>
#include <QCache>
#include <QDrag>
#include <QFont>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QtMath>

#include "VisualGroup.h"
#include "ui/themes/CatPainter.h"
#include "ui/themes/ThemeManager.h"

#include <Application.h>
#include <InstanceList.h>

namespace {
// Blamdom fork: "Cozy Night" welcome-card scene (design direction B, approved Nov 2026) —
// starfield, crescent moon, mountain ranges, a tent and a campfire with an ember glow.
// Drawn inside `box`; the bottom fades into the card body color (#0e1b20).
void drawBlamdomNightScene(QPainter& p, const QRectF& box)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(box);
    const qreal w = box.width(), h = box.height();
    const auto P = [&](qreal rx, qreal ry) { return QPointF(box.left() + rx * w, box.top() + ry * h); };

    // sky: slightly lighter at the top, blending down into the card
    QLinearGradient sky(0, box.top(), 0, box.bottom());
    sky.setColorAt(0.0, QColor("#0e2128"));
    sky.setColorAt(1.0, QColor("#0a1418"));
    p.fillRect(box, sky);

    // stars (fixed positions so the frame is stable)
    p.setPen(Qt::NoPen);
    const double stars[][3] = {
        { 0.07, 0.16, 0.70 }, { 0.15, 0.09, 0.50 }, { 0.24, 0.30, 0.60 }, { 0.36, 0.13, 0.45 },
        { 0.47, 0.26, 0.55 }, { 0.58, 0.07, 0.50 }, { 0.68, 0.30, 0.40 }, { 0.90, 0.20, 0.60 },
        { 0.11, 0.44, 0.40 }, { 0.44, 0.40, 0.40 }, { 0.94, 0.46, 0.45 }, { 0.29, 0.05, 0.45 },
        { 0.83, 0.52, 0.35 },
    };
    for (const auto& s : stars) {
        p.setBrush(QColor(231, 240, 234, int(60 + 120 * s[2])));
        p.drawEllipse(P(s[0], s[1]), 1.4, 1.4);
    }

    // crescent moon (upper right); the cut is tinted with the local sky color
    {
        const QPointF mc(P(0.80, 0.20));
        const qreal mr = qMin(w, h) * 0.10;
        p.setBrush(QColor("#f2ead2"));
        p.drawEllipse(mc, mr, mr);
        const qreal t = 0.20;  // moon's vertical position in the sky gradient
        const auto mix = [&](int top, int bot) { return top + int(t * (bot - top)); };
        p.setBrush(QColor(mix(0x0e, 0x0a), mix(0x21, 0x14), mix(0x28, 0x18)));
        p.drawEllipse(mc + QPointF(mr * 0.45, -mr * 0.14), mr * 0.94, mr * 0.94);
    }

    const QColor mBack("#0e1c21");
    const QColor mMid("#13262d");

    // back mountain range
    p.setPen(Qt::NoPen);
    p.setBrush(mBack);
    QPolygonF b1; b1 << P(0.00, 1.00) << P(0.15, 0.60) << P(0.31, 1.00);
    QPolygonF b2; b2 << P(0.25, 1.00) << P(0.43, 0.52) << P(0.61, 1.00);
    QPolygonF b3; b3 << P(0.56, 1.00) << P(0.77, 0.58) << P(0.97, 1.00);
    p.drawPolygon(b1);
    p.drawPolygon(b2);
    p.drawPolygon(b3);

    // front peak with a faint ember light on its left slope
    p.setBrush(mMid);
    QPolygonF front; front << P(0.30, 1.00) << P(0.51, 0.46) << P(0.73, 1.00);
    p.drawPolygon(front);
    p.setBrush(QColor(245, 165, 36, 24));
    QPolygonF lit; lit << P(0.51, 0.46) << P(0.41, 0.76) << P(0.50, 0.72);
    p.drawPolygon(lit);

    // ground strip
    p.setBrush(mBack);
    p.drawRect(QRectF(box.left(), box.bottom() - h * 0.06, w, h * 0.06));

    // tent (left)
    p.setBrush(mBack);
    p.setPen(QPen(QColor("#1e3a42"), 1));
    QPolygonF tent; tent << P(0.13, 0.945) << P(0.20, 0.79) << P(0.27, 0.945);
    p.drawPolygon(tent);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(245, 165, 36, 44));
    QPolygonF door; door << P(0.185, 0.945) << P(0.20, 0.86) << P(0.215, 0.945);
    p.drawPolygon(door);

    // campfire (right of center): glow, base, flame
    {
        const QPointF fc(P(0.62, 0.905));
        const qreal r = qMin(w, h) * 0.17;
        QRadialGradient glow(fc, r);
        glow.setColorAt(0.0, QColor(245, 165, 36, 74));
        glow.setColorAt(1.0, QColor(245, 165, 36, 0));
        p.fillRect(QRectF(fc.x() - r, fc.y() - r, 2 * r, 2 * r), glow);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(245, 165, 36));
        p.drawEllipse(fc, qMin(w, h) * 0.05, qMin(w, h) * 0.022);
        p.setBrush(QColor("#ffcf7a"));
        p.drawEllipse(fc + QPointF(0, -qMin(w, h) * 0.032), qMin(w, h) * 0.030, qMin(w, h) * 0.055);
        p.setBrush(QColor("#fff3d9"));
        p.drawEllipse(fc + QPointF(0, -qMin(w, h) * 0.018), qMin(w, h) * 0.016, qMin(w, h) * 0.030);
    }

    // fade the bottom of the scene into the card body color
    QLinearGradient fade(0, box.bottom() - h * 0.30, 0, box.bottom());
    fade.setColorAt(0.0, QColor(14, 27, 32, 0));
    fade.setColorAt(1.0, QColor(14, 27, 32, 255));
    p.fillRect(QRectF(box.left(), box.bottom() - h * 0.30, w, h * 0.30), fade);

    p.restore();
}
}  // namespace

InstanceView::InstanceView(QWidget* parent) : QAbstractItemView(parent)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAcceptDrops(true);
    setAutoScroll(true);
    setPaintCat(APPLICATION->settings()->get("TheCat").toBool());
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), QOverload<>::of(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), QOverload<>::of(&QWidget::update));
}

InstanceView::~InstanceView()
{
    qDeleteAll(m_groups);
    m_groups.clear();
    if (m_cat) {
        m_cat->deleteLater();
    }
}

void InstanceView::setModel(QAbstractItemModel* model)
{
    QAbstractItemView::setModel(model);
    connect(model, &QAbstractItemModel::modelReset, this, &InstanceView::modelReset);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &InstanceView::rowsRemoved);
}

void InstanceView::dataChanged([[maybe_unused]] const QModelIndex& topLeft,
                               [[maybe_unused]] const QModelIndex& bottomRight,
                               [[maybe_unused]] const QList<int>& roles)
{
    scheduleDelayedItemsLayout();
}
void InstanceView::rowsInserted([[maybe_unused]] const QModelIndex& parent, [[maybe_unused]] int start, [[maybe_unused]] int end)
{
    scheduleDelayedItemsLayout();
}

void InstanceView::rowsAboutToBeRemoved([[maybe_unused]] const QModelIndex& parent, [[maybe_unused]] int start, [[maybe_unused]] int end)
{
    scheduleDelayedItemsLayout();
}

void InstanceView::modelReset()
{
    scheduleDelayedItemsLayout();
}

void InstanceView::rowsRemoved()
{
    scheduleDelayedItemsLayout();
}

void InstanceView::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    QAbstractItemView::currentChanged(current, previous);
    // TODO: for accessibility support, implement+register a factory, steal QAccessibleTable from Qt and return an instance of it for
    // InstanceView.
#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive() && current.isValid()) {
        QAccessibleEvent event(this, QAccessible::Focus);
        event.setChild(current.row());
        QAccessible::updateAccessibility(&event);
    }
#endif /* !QT_NO_ACCESSIBILITY */
}

class LocaleString : public QString {
   public:
    LocaleString(const char* s) : QString(s) {}
    LocaleString(const QString& s) : QString(s) {}
};

inline bool operator<(const LocaleString& lhs, const LocaleString& rhs)
{
    return (QString::localeAwareCompare(lhs, rhs) < 0);
}

void InstanceView::updateScrollbar()
{
    int previousScroll = verticalScrollBar()->value();
    if (m_groups.isEmpty()) {
        verticalScrollBar()->setRange(0, 0);
    } else {
        int totalHeight = 0;
        // top margin
        totalHeight += m_categoryMargin;
        int itemScroll = 0;
        for (auto category : m_groups) {
            category->m_verticalPosition = totalHeight;
            totalHeight += category->totalHeight() + m_categoryMargin;
            if (!itemScroll && category->totalHeight() != 0) {
                itemScroll = category->contentHeight() / category->numRows();
            }
        }
        // do not divide by zero
        if (itemScroll == 0)
            itemScroll = 64;

        totalHeight += m_bottomMargin;
        verticalScrollBar()->setSingleStep(itemScroll);
        const int rowsPerPage = qMax(viewport()->height() / itemScroll, 1);
        verticalScrollBar()->setPageStep(rowsPerPage * itemScroll);

        verticalScrollBar()->setRange(0, totalHeight - height());
    }

    verticalScrollBar()->setValue(qMin(previousScroll, verticalScrollBar()->maximum()));
}

void InstanceView::updateGeometries()
{
    m_geometryCache.clear();

    QMap<LocaleString, VisualGroup*> cats;

    for (int i = 0; i < model()->rowCount(); ++i) {
        const QString groupName = model()->index(i, 0).data(InstanceViewRoles::GroupRole).toString();
        if (!cats.contains(groupName)) {
            VisualGroup* old = this->category(groupName);
            if (old) {
                auto cat = new VisualGroup(old);
                cats.insert(groupName, cat);
                cat->update();
            } else {
                auto cat = new VisualGroup(groupName, this);
                if (m_fVisibility) {
                    cat->collapsed = m_fVisibility(groupName);
                }
                cats.insert(groupName, cat);
                cat->update();
            }
        }
    }

    qDeleteAll(m_groups);
    m_groups = cats.values();
    updateScrollbar();
    viewport()->update();
}

bool InstanceView::isIndexHidden(const QModelIndex& index) const
{
    VisualGroup* cat = category(index);
    if (cat) {
        return cat->collapsed;
    } else {
        return false;
    }
}

VisualGroup* InstanceView::category(const QModelIndex& index) const
{
    return category(index.data(InstanceViewRoles::GroupRole).toString());
}

VisualGroup* InstanceView::category(const QString& cat) const
{
    for (auto group : m_groups) {
        if (group->text == cat) {
            return group;
        }
    }
    return nullptr;
}

VisualGroup* InstanceView::categoryAt(const QPoint& pos, VisualGroup::HitResults& result) const
{
    for (auto group : m_groups) {
        result = group->hitScan(pos);
        if (result != VisualGroup::NoHit) {
            return group;
        }
    }
    result = VisualGroup::NoHit;
    return nullptr;
}

QString InstanceView::groupNameAt(const QPoint& point)
{
    executeDelayedItemsLayout();

    VisualGroup::HitResults hitResult;
    auto group = categoryAt(point + offset(), hitResult);
    if (group && (hitResult & (VisualGroup::HeaderHit | VisualGroup::BodyHit))) {
        return group->text;
    }
    return QString();
}

int InstanceView::calculateItemsPerRow() const
{
    return qFloor((qreal)(contentWidth()) / (qreal)(itemWidth() + m_spacing));
}

int InstanceView::contentWidth() const
{
    return width() - m_leftMargin - m_rightMargin;
}

int InstanceView::itemWidth() const
{
    return m_itemWidth;
}

void InstanceView::mousePressEvent(QMouseEvent* event)
{
    // Blamdom fork: the welcome card's pill triggers Add Instance
    if (event->button() == Qt::LeftButton && model()->rowCount() == 0 && m_welcomePill.contains(event->pos())) {
        emit addInstanceRequested();
        event->accept();
        return;
    }

    executeDelayedItemsLayout();

    QPoint visualPos = event->pos();
    QPoint geometryPos = event->pos() + offset();

    QPersistentModelIndex index = indexAt(visualPos);

    m_pressedIndex = index;
    m_pressedAlreadySelected = selectionModel()->isSelected(m_pressedIndex);
    m_pressedPosition = geometryPos;

    if (event->button() == Qt::LeftButton) {
        VisualGroup::HitResults hitResult;
        m_pressedCategory = categoryAt(geometryPos, hitResult);
        if (m_pressedCategory && hitResult & VisualGroup::CheckboxHit) {
            setState(m_pressedCategory->collapsed ? ExpandingState : CollapsingState);
            event->accept();
            return;
        }
    }

    if (index.isValid() && (index.flags() & Qt::ItemIsEnabled)) {
        if (index != currentIndex()) {
            // FIXME: better!
            m_currentCursorColumn = -1;
        }
        // we disable scrollTo for mouse press so the item doesn't change position
        // when the user is interacting with it (ie. clicking on it)
        bool autoScroll = hasAutoScroll();
        setAutoScroll(false);
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);

        setAutoScroll(autoScroll);
        QRect rect(visualPos, visualPos);
        setSelection(rect, QItemSelectionModel::ClearAndSelect);

        // signal handlers may change the model
        emit pressed(index);
    } else {
        // Forces a finalize() even if mouse is pressed, but not on a item
        selectionModel()->select(QModelIndex(), QItemSelectionModel::Select);
    }
}

void InstanceView::mouseMoveEvent(QMouseEvent* event)
{
    executeDelayedItemsLayout();

    QPoint topLeft;
    QPoint visualPos = event->pos();
    QPoint geometryPos = event->pos() + offset();

    if (state() == ExpandingState || state() == CollapsingState) {
        return;
    }

    if (state() == DraggingState) {
        topLeft = m_pressedPosition - offset();
        if ((topLeft - event->pos()).manhattanLength() > QApplication::startDragDistance()) {
            m_pressedIndex = QModelIndex();
            startDrag(model()->supportedDragActions());
            setState(NoState);
            stopAutoScroll();
        }
        return;
    }

    if (selectionMode() != SingleSelection) {
        topLeft = m_pressedPosition - offset();
    } else {
        topLeft = geometryPos;
    }

    if (m_pressedIndex.isValid() && (state() != DragSelectingState) && (event->buttons() != Qt::NoButton) && !selectedIndexes().isEmpty()) {
        setState(DraggingState);
        return;
    }

    if ((event->buttons() & Qt::LeftButton) && selectionModel()) {
        setState(DragSelectingState);

        setSelection(QRect(visualPos, visualPos), QItemSelectionModel::ClearAndSelect);
        QModelIndex index = indexAt(visualPos);

        // set at the end because it might scroll the view
        if (index.isValid() && (index != selectionModel()->currentIndex()) && (index.flags() & Qt::ItemIsEnabled)) {
            selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        }
    }
}

void InstanceView::mouseReleaseEvent(QMouseEvent* event)
{
    executeDelayedItemsLayout();

    QPoint visualPos = event->pos();
    QPoint geometryPos = event->pos() + offset();
    QPersistentModelIndex index = indexAt(visualPos);

    VisualGroup::HitResults hitResult;

    if (event->button() == Qt::LeftButton && m_pressedCategory != nullptr && m_pressedCategory == categoryAt(geometryPos, hitResult)) {
        if (state() == ExpandingState) {
            m_pressedCategory->collapsed = false;
            emit groupStateChanged(m_pressedCategory->text, false);

            updateGeometries();
            viewport()->update();
            event->accept();
            m_pressedCategory = nullptr;
            setState(NoState);
            return;
        } else if (state() == CollapsingState) {
            m_pressedCategory->collapsed = true;
            emit groupStateChanged(m_pressedCategory->text, true);

            updateGeometries();
            viewport()->update();
            event->accept();
            m_pressedCategory = nullptr;
            setState(NoState);
            return;
        }
    }

    m_ctrlDragSelectionFlag = QItemSelectionModel::NoUpdate;

    setState(NoState);

    if (index == m_pressedIndex && index.isValid()) {
        if (event->button() == Qt::LeftButton) {
            emit clicked(index);
        }
        QStyleOptionViewItem option;
        initViewItemOption(&option);
        if (m_pressedAlreadySelected) {
            option.state |= QStyle::State_Selected;
        }
        if ((model()->flags(index) & Qt::ItemIsEnabled) &&
            style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, &option, this)) {
            emit activated(index);
        }
    }
}

void InstanceView::mouseDoubleClickEvent(QMouseEvent* event)
{
    executeDelayedItemsLayout();

    QModelIndex index = indexAt(event->pos());
    if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled) || (m_pressedIndex != index)) {
        QMouseEvent me(QEvent::MouseButtonPress, event->position(), event->scenePosition(), event->globalPosition(), event->button(),
                       event->buttons(), event->modifiers());
        mousePressEvent(&me);
        return;
    }
    // signal handlers may change the model
    QPersistentModelIndex persistent = index;
    emit doubleClicked(persistent);

    QStyleOptionViewItem option;
    initViewItemOption(&option);
    if ((model()->flags(index) & Qt::ItemIsEnabled) && !style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, &option, this)) {
        emit activated(index);
    }
}

void InstanceView::setPaintCat(bool visible)
{
    if (m_cat) {
        disconnect(m_cat, &CatPainter::updateFrame, this, nullptr);
        delete m_cat;
        m_cat = nullptr;
    }
    if (visible) {
        m_cat = new CatPainter(APPLICATION->themeManager()->getCatPack(), this);
        connect(m_cat, &CatPainter::updateFrame, this, [this] { viewport()->update(); });
    }
}

void InstanceView::paintEvent([[maybe_unused]] QPaintEvent* event)
{
    executeDelayedItemsLayout();

    QPainter painter(this->viewport());

    if (m_cat) {
        m_cat->paint(&painter, this->viewport()->rect());
    }

    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.widget = this;

    if (model()->rowCount() == 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        QRect bounds = viewport()->geometry();
        bounds.moveTop(0);

        // Blamdom fork: "Cozy Night" welcome card (design direction B, approved Nov 2026)
        m_welcomePill = {};

        const int cardW = qMin(560, qMax(320, bounds.width() - 60));
        const int cardH = qMin(470, qMax(300, bounds.height() - 60));
        const QRect card(bounds.center().x() - cardW / 2, bounds.center().y() - cardH / 2, cardW, cardH);
        const qreal radius = 20.0;

        // check if we are allowed to draw in our area
        if (!event->rect().intersects(card.adjusted(-4, -4, 4, 4))) {
            painter.restore();
            return;
        }

        // card body
        painter.setPen(QPen(QColor("#27464f"), 1));
        painter.setBrush(QColor("#0e1b20"));
        painter.drawRoundedRect(card, radius, radius);

        // night scene across the top, clipped to the rounded card
        painter.save();
        QPainterPath cardClip;
        cardClip.addRoundedRect(card, radius, radius);
        painter.setClipPath(cardClip);
        const QRectF scene(card.left(), card.top(), card.width(), card.height() * 0.50);
        drawBlamdomNightScene(painter, scene);
        painter.restore();

        // left-aligned text block under the scene
        const int pad = 32;
        int y = qRound(scene.bottom()) + 24;

        // eyebrow
        QFont eyebrowFont("sans", 11);
        eyebrowFont.setBold(true);
        painter.setFont(eyebrowFont);
        painter.setPen(QColor("#8fa8a0"));
        painter.drawText(QRectF(card.left() + pad, y, cardW - pad * 2, 16), Qt::AlignLeft | Qt::AlignTop,
                         tr("GOOD EVENING"));
        y += 30;

        // headline
        QFont headFont("sans", 22);
        headFont.setBold(true);
        painter.setFont(headFont);
        painter.setPen(QColor("#e7f0ea"));
        painter.drawText(QRectF(card.left() + pad, y, cardW - pad * 2, 34), Qt::AlignLeft | Qt::AlignTop,
                         tr("Fire's lit. The world's waiting."));
        y += 44;

        // sub-headline
        QFont bodyFont("sans", 13);
        painter.setFont(bodyFont);
        painter.setPen(QColor("#8fa8a0"));
        painter.drawText(QRectF(card.left() + pad, y, cardW - pad * 2, 22), Qt::AlignLeft | Qt::AlignTop,
                         tr("Your Blamdom worlds, one click from the bedrock."));
        y += 36;

        // primary pill button (left-aligned, ember amber)
        const QRect pill(card.left() + pad, y, 210, 44);
        m_welcomePill = pill;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#f5a524"));
        painter.drawRoundedRect(pill, 22, 22);
        QFont pillFont("sans", 14);
        pillFont.setBold(true);
        painter.setFont(pillFont);
        painter.setPen(QColor("#241503"));
        painter.drawText(pill, Qt::AlignCenter, tr("+ Add Instance"));

        painter.restore();
        return;
    }

    int wpWidth = viewport()->width();
    option.rect.setWidth(wpWidth);
    for (auto* category : m_groups) {
        int y = category->verticalPosition();
        y -= verticalOffset();
        QRect backup = option.rect;
        int height = category->totalHeight();
        option.rect.setTop(y);
        option.rect.setHeight(height);
        option.rect.setLeft(m_leftMargin);
        option.rect.setRight(wpWidth - m_rightMargin);
        category->drawHeader(&painter, option);
        option.rect = backup;
    }

    for (int i = 0; i < model()->rowCount(); ++i) {
        const QModelIndex index = model()->index(i, 0);
        if (isIndexHidden(index)) {
            continue;
        }
        Qt::ItemFlags flags = index.flags();
        option.rect = visualRect(index);
        option.features |= QStyleOptionViewItem::WrapText;
        if (flags & Qt::ItemIsSelectable && selectionModel()->isSelected(index)) {
            option.state |= selectionModel()->isSelected(index) ? QStyle::State_Selected : QStyle::State_None;
        } else {
            option.state &= ~QStyle::State_Selected;
        }
        option.state |= (index == currentIndex()) ? QStyle::State_HasFocus : QStyle::State_None;
        if (!(flags & Qt::ItemIsEnabled)) {
            option.state &= ~QStyle::State_Enabled;
        }
        itemDelegate()->paint(&painter, option, index);
    }

    /*
     * Drop indicators for manual reordering...
     */
#if 0
    if (!m_lastDragPosition.isNull())
    {
        std::pair<VisualGroup *, VisualGroup::HitResults> pair = rowDropPos(m_lastDragPosition);
        VisualGroup *category = pair.first;
        VisualGroup::HitResults row = pair.second;
        if (category)
        {
            int internalRow = row - category->firstItemIndex;
            QLine line;
            if (internalRow >= category->numItems())
            {
                QRect toTheRightOfRect = visualRect(category->lastItem());
                line = QLine(toTheRightOfRect.topRight(), toTheRightOfRect.bottomRight());
            }
            else
            {
                QRect toTheLeftOfRect = visualRect(model()->index(row, 0));
                line = QLine(toTheLeftOfRect.topLeft(), toTheLeftOfRect.bottomLeft());
            }
            painter.save();
            painter.setPen(QPen(Qt::black, 3));
            painter.drawLine(line);
            painter.restore();
        }
    }
#endif
}

void InstanceView::resizeEvent([[maybe_unused]] QResizeEvent* event)
{
    int newItemsPerRow = calculateItemsPerRow();
    if (newItemsPerRow != m_currentItemsPerRow) {
        m_currentCursorColumn = -1;
        m_currentItemsPerRow = newItemsPerRow;
        updateGeometries();
    } else {
        updateScrollbar();
    }
}

void InstanceView::dragEnterEvent(QDragEnterEvent* event)
{
    executeDelayedItemsLayout();

    if (!isDragEventAccepted(event)) {
        return;
    }
    m_lastDragPosition = event->position().toPoint() + offset();
    viewport()->update();
    event->accept();
}

void InstanceView::dragMoveEvent(QDragMoveEvent* event)
{
    executeDelayedItemsLayout();

    if (!isDragEventAccepted(event)) {
        return;
    }
    m_lastDragPosition = event->position().toPoint() + offset();
    viewport()->update();
    event->accept();
}

void InstanceView::dragLeaveEvent([[maybe_unused]] QDragLeaveEvent* event)
{
    executeDelayedItemsLayout();

    m_lastDragPosition = QPoint();
    viewport()->update();
}

void InstanceView::dropEvent(QDropEvent* event)
{
    executeDelayedItemsLayout();

    m_lastDragPosition = QPoint();

    stopAutoScroll();
    setState(NoState);

    auto mimedata = event->mimeData();

    if (event->source() == this) {
        if (event->possibleActions() & Qt::MoveAction) {
            std::pair<VisualGroup*, VisualGroup::HitResults> dropPos = rowDropPos(event->position().toPoint());
            const VisualGroup* group = dropPos.first;
            auto hitResult = dropPos.second;

            if (hitResult == VisualGroup::HitResult::NoHit) {
                viewport()->update();
                return;
            }
            auto instanceId = QString::fromUtf8(mimedata->data("application/x-instanceid"));
            auto instanceList = APPLICATION->instances();
            instanceList->setInstanceGroup(instanceId, group->text);
            event->setDropAction(Qt::MoveAction);
            event->accept();

            updateGeometries();
            viewport()->update();
        }
        return;
    }

    // check if the action is supported
    if (!mimedata) {
        return;
    }

    // files dropped from outside?
    if (mimedata->hasUrls()) {
        auto urls = mimedata->urls();
        event->accept();
        emit droppedURLs(urls);
    }
}

void InstanceView::startDrag(Qt::DropActions supportedActions)
{
    executeDelayedItemsLayout();

    QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (indexes.count() == 0)
        return;

    QMimeData* mimeData = model()->mimeData(indexes);
    if (!mimeData) {
        return;
    }
    QRect rect;
    QPixmap pixmap = renderToPixmap(indexes, &rect);
    QDrag* drag = new QDrag(this);
    drag->setPixmap(pixmap);
    drag->setMimeData(mimeData);
    drag->setHotSpot(m_pressedPosition - rect.topLeft());
    Qt::DropAction defaultDropAction = Qt::IgnoreAction;
    if (this->defaultDropAction() != Qt::IgnoreAction && (supportedActions & this->defaultDropAction())) {
        defaultDropAction = this->defaultDropAction();
    }
    /*auto action = */
    drag->exec(supportedActions, defaultDropAction);
}

QRect InstanceView::visualRect(const QModelIndex& index) const
{
    const_cast<InstanceView*>(this)->executeDelayedItemsLayout();

    return geometryRect(index).translated(-offset());
}

QRect InstanceView::geometryRect(const QModelIndex& index) const
{
    const_cast<InstanceView*>(this)->executeDelayedItemsLayout();

    if (!index.isValid() || isIndexHidden(index) || index.column() > 0) {
        return QRect();
    }

    int row = index.row();
    if (m_geometryCache.contains(row)) {
        return *m_geometryCache[row];
    }

    const VisualGroup* cat = category(index);
    QPair<int, int> pos = cat->positionOf(index);
    int x = pos.first;
    // int y = pos.second;

    QStyleOptionViewItem option;
    initViewItemOption(&option);

    QRect out;
    out.setTop(cat->verticalPosition() + cat->headerHeight() + 5 + cat->rowTopOf(index));
    out.setLeft(m_spacing + x * (itemWidth() + m_spacing));
    out.setSize(itemDelegate()->sizeHint(option, index));
    m_geometryCache.insert(row, new QRect(out));
    return out;
}

QModelIndex InstanceView::indexAt(const QPoint& point) const
{
    const_cast<InstanceView*>(this)->executeDelayedItemsLayout();

    for (int i = 0; i < model()->rowCount(); ++i) {
        QModelIndex index = model()->index(i, 0);
        if (visualRect(index).contains(point)) {
            return index;
        }
    }
    return QModelIndex();
}

void InstanceView::setSelection(const QRect& rect, const QItemSelectionModel::SelectionFlags commands)
{
    executeDelayedItemsLayout();

    for (int i = 0; i < model()->rowCount(); ++i) {
        QModelIndex index = model()->index(i, 0);
        QRect itemRect = visualRect(index);
        if (itemRect.intersects(rect)) {
            selectionModel()->select(index, commands);
            update(itemRect.translated(-offset()));
        }
    }
}

QPixmap InstanceView::renderToPixmap(const QModelIndexList& indices, QRect* r) const
{
    Q_ASSERT(r);
    auto paintPairs = draggablePaintPairs(indices, r);
    if (paintPairs.isEmpty()) {
        return QPixmap();
    }
    QPixmap pixmap(r->size());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.state |= QStyle::State_Selected;
    for (int j = 0; j < paintPairs.count(); ++j) {
        option.rect = paintPairs.at(j).first.translated(-r->topLeft());
        const QModelIndex& current = paintPairs.at(j).second;
        itemDelegate()->paint(&painter, option, current);
    }
    return pixmap;
}

QList<std::pair<QRect, QModelIndex>> InstanceView::draggablePaintPairs(const QModelIndexList& indices, QRect* r) const
{
    Q_ASSERT(r);
    QRect& rect = *r;
    QList<std::pair<QRect, QModelIndex>> ret;
    for (int i = 0; i < indices.count(); ++i) {
        const QModelIndex& index = indices.at(i);
        const QRect current = geometryRect(index);
        ret += std::make_pair(current, index);
        rect |= current;
    }
    return ret;
}

bool InstanceView::isDragEventAccepted([[maybe_unused]] QDropEvent* event)
{
    return true;
}

std::pair<VisualGroup*, VisualGroup::HitResults> InstanceView::rowDropPos(const QPoint& pos)
{
    VisualGroup::HitResults hitResult;
    auto group = categoryAt(pos + offset(), hitResult);
    return std::make_pair(group, hitResult);
}

QPoint InstanceView::offset() const
{
    return QPoint(horizontalOffset(), verticalOffset());
}

QRegion InstanceView::visualRegionForSelection(const QItemSelection& selection) const
{
    QRegion region;
    for (auto& range : selection) {
        int start_row = range.top();
        int end_row = range.bottom();
        for (int row = start_row; row <= end_row; ++row) {
            int start_column = range.left();
            int end_column = range.right();
            for (int column = start_column; column <= end_column; ++column) {
                QModelIndex index = model()->index(row, column, rootIndex());
                region += visualRect(index);  // OK
            }
        }
    }
    return region;
}

QModelIndex InstanceView::moveCursor(QAbstractItemView::CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    auto current = currentIndex();
    if (!current.isValid()) {
        return current;
    }
    auto cat = category(current);
    int group_index = m_groups.indexOf(cat);
    if (group_index < 0)
        return current;

    QPair<int, int> pos = cat->positionOf(current);
    int column = pos.first;
    int row = pos.second;
    if (m_currentCursorColumn < 0) {
        m_currentCursorColumn = column;
    }
    // Handle different movement actions.
    switch (cursorAction) {
        case MoveUp: {
            if (row == 0) {
                int prevGroupIndex = group_index - 1;
                while (prevGroupIndex >= 0) {
                    auto prevGroup = m_groups[prevGroupIndex];
                    if (prevGroup->collapsed) {
                        prevGroupIndex--;
                        continue;
                    }
                    int newRow = prevGroup->numRows() - 1;
                    int newRowSize = prevGroup->rows[newRow].size();
                    int newColumn = m_currentCursorColumn;
                    if (m_currentCursorColumn >= newRowSize) {
                        newColumn = newRowSize - 1;
                    }
                    return prevGroup->rows[newRow][newColumn];
                }
            } else {
                int newRow = row - 1;
                int newRowSize = cat->rows[newRow].size();
                int newColumn = m_currentCursorColumn;
                if (m_currentCursorColumn >= newRowSize) {
                    newColumn = newRowSize - 1;
                }
                return cat->rows[newRow][newColumn];
            }
            return current;
        }
        case MoveDown: {
            if (row == cat->rows.size() - 1) {
                int nextGroupIndex = group_index + 1;
                while (nextGroupIndex < m_groups.size()) {
                    auto nextGroup = m_groups[nextGroupIndex];
                    if (nextGroup->collapsed) {
                        nextGroupIndex++;
                        continue;
                    }
                    int newRowSize = nextGroup->rows[0].size();
                    int newColumn = m_currentCursorColumn;
                    if (m_currentCursorColumn >= newRowSize) {
                        newColumn = newRowSize - 1;
                    }
                    return nextGroup->rows[0][newColumn];
                }
            } else {
                int newRow = row + 1;
                int newRowSize = cat->rows[newRow].size();
                int newColumn = m_currentCursorColumn;
                if (m_currentCursorColumn >= newRowSize) {
                    newColumn = newRowSize - 1;
                }
                return cat->rows[newRow][newColumn];
            }
            return current;
        }
        case MoveLeft: {
            if (column > 0) {
                m_currentCursorColumn = column - 1;
                return cat->rows[row][column - 1];
            } else if (row > 0) {
                row -= 1;
                int newRowSize = cat->rows[row].size();
                m_currentCursorColumn = newRowSize - 1;
                return cat->rows[row][m_currentCursorColumn];
            } else {
                int prevGroupIndex = group_index - 1;
                while (prevGroupIndex >= 0) {
                    auto prevGroup = m_groups[prevGroupIndex];
                    if (prevGroup->collapsed) {
                        prevGroupIndex--;
                        continue;
                    }
                    int lastRow = prevGroup->numRows() - 1;
                    int lastCol = prevGroup->rows[lastRow].size() - 1;
                    m_currentCursorColumn = lastCol;
                    return prevGroup->rows[lastRow][lastCol];
                }
            }
            return current;
        }
        case MoveRight: {
            if (column < cat->rows[row].size() - 1) {
                m_currentCursorColumn = column + 1;
                return cat->rows[row][column + 1];
            } else if (row < cat->rows.size() - 1) {
                row += 1;
                m_currentCursorColumn = 0;
                return cat->rows[row][m_currentCursorColumn];
            } else {
                int nextGroupIndex = group_index + 1;
                while (nextGroupIndex < m_groups.size()) {
                    auto nextGroup = m_groups[nextGroupIndex];
                    if (nextGroup->collapsed) {
                        nextGroupIndex++;
                        continue;
                    }
                    m_currentCursorColumn = 0;
                    return nextGroup->rows[0][0];
                }
            }
            return current;
        }
        case MoveHome: {
            m_currentCursorColumn = 0;
            return cat->rows[row][0];
        }
        case MoveEnd: {
            auto last = cat->rows[row].size() - 1;
            m_currentCursorColumn = last;
            return cat->rows[row][last];
        }
        default:
            // For unsupported cursor actions, return the current index.
            break;
    }
    return current;
}

int InstanceView::horizontalOffset() const
{
    return horizontalScrollBar()->value();
}

int InstanceView::verticalOffset() const
{
    return verticalScrollBar()->value();
}

void InstanceView::scrollContentsBy(int dx, int dy)
{
    scrollDirtyRegion(dx, dy);
    viewport()->scroll(dx, dy);
}

void InstanceView::scrollTo(const QModelIndex& index, ScrollHint hint)
{
    if (!index.isValid())
        return;

    const QRect rect = visualRect(index);
    if (hint == EnsureVisible && viewport()->rect().contains(rect)) {
        viewport()->update(rect);
        return;
    }

    verticalScrollBar()->setValue(verticalScrollToValue(index, rect, hint));
}

int InstanceView::verticalScrollToValue([[maybe_unused]] const QModelIndex& index, const QRect& rect, QListView::ScrollHint hint) const
{
    const QRect area = viewport()->rect();
    const bool above = (hint == QListView::EnsureVisible && rect.top() < area.top());
    const bool below = (hint == QListView::EnsureVisible && rect.bottom() > area.bottom());

    int verticalValue = verticalScrollBar()->value();
    QRect adjusted = rect.adjusted(-spacing(), -spacing(), spacing(), spacing());
    if (hint == QListView::PositionAtTop || above)
        verticalValue += adjusted.top();
    else if (hint == QListView::PositionAtBottom || below)
        verticalValue += qMin(adjusted.top(), adjusted.bottom() - area.height() + 1);
    else if (hint == QListView::PositionAtCenter)
        verticalValue += adjusted.top() - ((area.height() - adjusted.height()) / 2);
    return verticalValue;
}
