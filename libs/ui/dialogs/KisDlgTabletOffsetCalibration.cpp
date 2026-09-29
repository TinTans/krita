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

// tilt (in degrees) above which we consider that the stylus reports tilt
const qreal tiltReportedThreshold = 3.0;

// minimal tilt (in degrees) expected for the "lean the stylus" steps
const qreal minLeanAngle = 15.0;

qreal tiltAngle(const QPointF &tilt)
{
    return qSqrt(tilt.x() * tilt.x() + tilt.y() * tilt.y());
}
}

KisDlgTabletOffsetCalibration::KisDlgTabletOffsetCalibration(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Stylus Position Calibration"));
    setWindowState(windowState() | Qt::WindowFullScreen);

    const QString normalGrip = i18n("Hold the stylus the way you normally draw.");

    m_steps << Step{QPointF(0.5, 0.5), normalGrip, false}
            << Step{QPointF(0.5, 0.5), i18n("Lean the top of the stylus to the LEFT, as far as is comfortable."), true}
            << Step{QPointF(0.5, 0.5), i18n("Lean the top of the stylus to the RIGHT, as far as is comfortable."), true}
            << Step{QPointF(0.5, 0.5), i18n("Lean the top of the stylus TOWARDS YOU (the bottom of the screen)."), true}
            << Step{QPointF(0.5, 0.5), i18n("Lean the top of the stylus AWAY FROM YOU (the top of the screen)."), true}
            << Step{QPointF(0.2, 0.2), normalGrip, false}
            << Step{QPointF(0.8, 0.2), normalGrip, false}
            << Step{QPointF(0.8, 0.8), normalGrip, false}
            << Step{QPointF(0.2, 0.8), normalGrip, false};

    m_btnRestart = new QPushButton(i18n("Restart"), this);
    connect(m_btnRestart, SIGNAL(clicked()), SLOT(slotRestart()));

    m_btnCancel = new QPushButton(i18n("Cancel"), this);
    connect(m_btnCancel, SIGNAL(clicked()), SLOT(reject()));

    updateButtonsGeometry();
}

KisDlgTabletOffsetCalibration::~KisDlgTabletOffsetCalibration()
{
}

KisTabletPositionCorrection KisDlgTabletOffsetCalibration::correction() const
{
    return m_correction;
}

KisTabletPositionCorrection::FitInfo KisDlgTabletOffsetCalibration::fitInfo() const
{
    return m_fitInfo;
}

QPointF KisDlgTabletOffsetCalibration::targetPosition(int index) const
{
    const QPointF &relative = m_steps[index].relativeTarget;
    return QPointF(relative.x() * width(), relative.y() * height());
}

void KisDlgTabletOffsetCalibration::slotRestart()
{
    m_samples.clear();
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

    const QPointF tilt(event->xTilt(), event->yTilt());

    if (event->type() == QEvent::TabletMove || event->type() == QEvent::TabletPress) {
        m_currentTilt = tilt;
        if (tiltAngle(tilt) > tiltReportedThreshold) {
            m_tiltReported = true;
        }
        update();
    }

    // let the stylus press the buttons: the event will be
    // converted into a mouse click by Qt
    if (event->type() != QEvent::TabletPress || childAt(pos.toPoint())) {
        event->ignore();
        return;
    }

    event->accept();

    const int index = m_samples.size();
    if (index >= m_steps.size()) return;

    const QPointF delta = targetPosition(index) - pos;
    const qreal maxAllowedDeviation =
        maxAllowedDeviationPortion * qMin(width(), height());

    if (qSqrt(delta.x() * delta.x() + delta.y() * delta.y()) > maxAllowedDeviation) {
        m_message = i18n("That tap was too far from the crosshair, please try again.");
        update();
        return;
    }

    // if the stylus reports tilt at all, make sure the user actually leaned it
    if (m_steps[index].requiresLean && m_tiltReported && tiltAngle(tilt) < minLeanAngle) {
        m_message = i18n("The stylus was held almost upright, please lean it more and tap again.");
        update();
        return;
    }

    m_message.clear();

    KisTabletPositionCorrection::Sample sample;
    sample.delta = delta;
    sample.xTilt = tilt.x();
    sample.yTilt = tilt.y();
    m_samples.append(sample);

    if (m_samples.size() == m_steps.size()) {
        m_correction = KisTabletPositionCorrection::fit(m_samples, &m_fitInfo);
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

    const int currentIndex = qMin(m_samples.size(), m_steps.size() - 1);

    QString instructions =
        i18n("Tap the center of the crosshair with the tip of your stylus (%1 of %2).",
             currentIndex + 1, m_steps.size());
    instructions += "\n" + m_steps[currentIndex].instruction;

    if (m_tiltReported) {
        instructions += "\n\n" + i18n("Current stylus tilt: %1°", qRound(tiltAngle(m_currentTilt)));
    }

    if (!m_message.isEmpty()) {
        instructions += "\n\n" + m_message;
    }

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 1.3);
    painter.setFont(font);

    const QRect textRect = rect().adjusted(20, 20, -20, -20);
    painter.setPen(palette().windowText().color());
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, instructions);

    // mark the positions which are already done
    QColor doneColor = palette().windowText().color();
    doneColor.setAlphaF(0.3);
    painter.setPen(QPen(doneColor, 1));
    for (int i = 0; i < m_samples.size(); i++) {
        const QPointF done = targetPosition(i);
        painter.drawEllipse(done, crosshairSize / 3, crosshairSize / 3);
    }

    const QPointF target = targetPosition(currentIndex);

    QPen pen(palette().highlight().color(), 2);
    painter.setPen(pen);
    painter.drawLine(target - QPointF(crosshairSize, 0), target + QPointF(crosshairSize, 0));
    painter.drawLine(target - QPointF(0, crosshairSize), target + QPointF(0, crosshairSize));
    painter.drawEllipse(target, crosshairSize / 3, crosshairSize / 3);
}
