/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisDlgTabletOffsetCalibration.h"

#include <QPainter>
#include <QPushButton>
#include <QTabletEvent>
#include <QtMath>

#include <klocalizedstring.h>

namespace {
// size of the crosshair arms in logical pixels
const qreal crosshairSize = 30.0;

// taps further than this fraction of the smaller screen dimension from the
// target are considered to be mistakes, not a calibration error
const qreal maxAllowedDeviationPortion = 0.1;
}

KisDlgTabletOffsetCalibration::KisDlgTabletOffsetCalibration(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Stylus Position Calibration"));
    setWindowState(windowState() | Qt::WindowFullScreen);

    m_relativeTargets << QPointF(0.5, 0.5)
                      << QPointF(0.2, 0.2)
                      << QPointF(0.8, 0.2)
                      << QPointF(0.8, 0.8)
                      << QPointF(0.2, 0.8);

    m_btnRestart = new QPushButton(i18n("Restart"), this);
    connect(m_btnRestart, SIGNAL(clicked()), SLOT(slotRestart()));

    m_btnCancel = new QPushButton(i18n("Cancel"), this);
    connect(m_btnCancel, SIGNAL(clicked()), SLOT(reject()));

    updateButtonsGeometry();
}

KisDlgTabletOffsetCalibration::~KisDlgTabletOffsetCalibration()
{
}

QPointF KisDlgTabletOffsetCalibration::offset() const
{
    return m_offset;
}

QPointF KisDlgTabletOffsetCalibration::targetPosition(int index) const
{
    const QPointF &relative = m_relativeTargets[index];
    return QPointF(relative.x() * width(), relative.y() * height());
}

void KisDlgTabletOffsetCalibration::slotRestart()
{
    m_measuredDeltas.clear();
    m_message.clear();
    update();
}

void KisDlgTabletOffsetCalibration::updateButtonsGeometry()
{
    const int margin = 20;

    m_btnCancel->adjustSize();
    m_btnRestart->adjustSize();

    const int y = height() - margin - m_btnCancel->height();
    m_btnCancel->move(width() / 2 + margin / 2, y);
    m_btnRestart->move(width() / 2 - margin / 2 - m_btnRestart->width(), y);
}

void KisDlgTabletOffsetCalibration::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateButtonsGeometry();

    // the targets have moved, the old measurements are no longer valid
    slotRestart();
}

void KisDlgTabletOffsetCalibration::tabletEvent(QTabletEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF pos = event->position();
#else
    const QPointF pos = event->posF();
#endif

    // let the stylus press the buttons: the event will be
    // converted into a mouse click by Qt
    if (event->type() != QEvent::TabletPress || childAt(pos.toPoint())) {
        event->ignore();
        return;
    }

    event->accept();

    const int index = m_measuredDeltas.size();
    if (index >= m_relativeTargets.size()) return;

    const QPointF delta = targetPosition(index) - pos;
    const qreal maxAllowedDeviation =
        maxAllowedDeviationPortion * qMin(width(), height());

    if (qSqrt(delta.x() * delta.x() + delta.y() * delta.y()) > maxAllowedDeviation) {
        m_message = i18n("That tap was too far from the crosshair, please try again.");
        update();
        return;
    }

    m_message.clear();
    m_measuredDeltas.append(delta);

    if (m_measuredDeltas.size() == m_relativeTargets.size()) {
        QPointF sum;
        Q_FOREACH (const QPointF &d, m_measuredDeltas) {
            sum += d;
        }
        m_offset = sum / m_measuredDeltas.size();
        accept();
        return;
    }

    update();
}

void KisDlgTabletOffsetCalibration::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());

    const int currentIndex = m_measuredDeltas.size();

    const QString instructions =
        i18n("Tap the center of the crosshair with the tip of your stylus (%1 of %2).\n"
             "Hold the stylus the way you normally draw.",
             qMin(currentIndex + 1, m_relativeTargets.size()),
             m_relativeTargets.size());

    const QRect textRect = rect().adjusted(20, 20, -20, -20);
    painter.setPen(palette().windowText().color());
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                     m_message.isEmpty() ? instructions : instructions + "\n\n" + m_message);

    if (currentIndex >= m_relativeTargets.size()) return;

    const QPointF target = targetPosition(currentIndex);

    QPen pen(palette().highlight().color(), 2);
    painter.setPen(pen);
    painter.drawLine(target - QPointF(crosshairSize, 0), target + QPointF(crosshairSize, 0));
    painter.drawLine(target - QPointF(0, crosshairSize), target + QPointF(0, crosshairSize));
    painter.drawEllipse(target, crosshairSize / 3, crosshairSize / 3);

    // mark the targets which are already done
    QColor doneColor = palette().windowText().color();
    doneColor.setAlphaF(0.3);
    painter.setPen(QPen(doneColor, 1));
    for (int i = 0; i < currentIndex; i++) {
        const QPointF done = targetPosition(i);
        painter.drawEllipse(done, crosshairSize / 3, crosshairSize / 3);
    }
}
