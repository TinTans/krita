/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTabletPositionCorrector.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWindow>

#include <kis_config_notifier.h>

namespace {

// how close (in pixels and milliseconds) a mouse event must be to the last
// tablet event to be considered synthesized from it
const qreal maxSynthesizedMouseDistance = 1.5;
const ulong maxSynthesizedMouseDelay = 100;

/**
 * Shift the positions of events in-place. We cannot just construct new
 * events, because the copies would lose their "spontaneous" flag, which
 * Qt and some tools rely on to tell real user input from synthetic events.
 * Positions are protected members, so they are accessed via
 * pointers-to-members taken through derived classes.
 */
struct TabletEventPositionShifter : public QTabletEvent
{
    static void shift(QTabletEvent *event, const QPointF &offset)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QTabletEvent shifted(event->type(),
                             event->pointingDevice(),
                             event->position() + offset,
                             event->globalPosition() + offset,
                             event->pressure(),
                             event->xTilt(),
                             event->yTilt(),
                             event->tangentialPressure(),
                             event->rotation(),
                             event->z(),
                             event->modifiers(),
                             event->button(),
                             event->buttons());
        shifted.setTimestamp(event->timestamp());
        event->*(&TabletEventPositionShifter::m_points) = shifted.points();
#else
        event->*(&TabletEventPositionShifter::mPos) += offset;
        event->*(&TabletEventPositionShifter::mGPos) += offset;
#endif
    }
};

struct MouseEventPositionShifter : public QMouseEvent
{
    static void shift(QMouseEvent *event, const QPointF &offset)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QMouseEvent shifted(event->type(),
                            event->position() + offset,
                            event->scenePosition() + offset,
                            event->globalPosition() + offset,
                            event->button(),
                            event->buttons(),
                            event->modifiers(),
                            event->pointingDevice());
        shifted.setTimestamp(event->timestamp());
        event->*(&MouseEventPositionShifter::m_points) = shifted.points();
#else
        event->*(&MouseEventPositionShifter::l) += offset;
        event->*(&MouseEventPositionShifter::w) += offset;
        event->*(&MouseEventPositionShifter::s) += offset;
#endif
    }
};

QPointF globalPosOf(const QTabletEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition();
#else
    return event->globalPosF();
#endif
}

QPointF globalPosOf(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition();
#else
    return event->screenPos();
#endif
}

bool isTabletEvent(QEvent::Type type)
{
    return type == QEvent::TabletPress ||
           type == QEvent::TabletMove ||
           type == QEvent::TabletRelease;
}

bool isMouseEvent(QEvent::Type type)
{
    return type == QEvent::MouseButtonPress ||
           type == QEvent::MouseMove ||
           type == QEvent::MouseButtonRelease ||
           type == QEvent::MouseButtonDblClick;
}

}

KisTabletPositionCorrector *KisTabletPositionCorrector::instance()
{
    static KisTabletPositionCorrector *s_instance = nullptr;

    if (!s_instance && QCoreApplication::instance()) {
        s_instance = new KisTabletPositionCorrector();
        s_instance->setParent(QCoreApplication::instance());
        QCoreApplication::instance()->installEventFilter(s_instance);
    }

    return s_instance;
}

KisTabletPositionCorrector::KisTabletPositionCorrector()
{
    connect(KisConfigNotifier::instance(), SIGNAL(configChanged()), SLOT(slotConfigChanged()));
}

KisTabletPositionCorrector::SuspendGuard::SuspendGuard()
{
    KisTabletPositionCorrector *corrector = KisTabletPositionCorrector::instance();
    if (corrector) {
        corrector->m_suspendCount++;
        corrector->m_hasLastTabletEvent = false;
    }
}

KisTabletPositionCorrector::SuspendGuard::~SuspendGuard()
{
    KisTabletPositionCorrector *corrector = KisTabletPositionCorrector::instance();
    if (corrector) {
        corrector->m_suspendCount--;
    }
}

void KisTabletPositionCorrector::slotConfigChanged()
{
    // reloaded (for the current screen orientation) on the next tablet event
    m_correctionOrientation = -1;
}

void KisTabletPositionCorrector::updateCorrectionForOrientation()
{
    // the correction is calibrated separately for each screen orientation
    const int orientation = KisTabletPositionCorrection::currentScreenOrientation();
    if (orientation != m_correctionOrientation) {
        m_correction = KisTabletPositionCorrection::fromConfig(false, orientation);
        m_correctionOrientation = orientation;
    }
}

bool KisTabletPositionCorrector::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Only handle the events when they are delivered to their top-level
    // window, i.e. once, before Qt dispatches them to the widgets
    if (!(isTabletEvent(type) || isMouseEvent(type)) ||
        m_suspendCount > 0 ||
        !qobject_cast<QWindow*>(watched)) {

        return false;
    }

    if (isTabletEvent(type)) {
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);

        updateCorrectionForOrientation();
        const QPointF shift = m_correction.isNull()
            ? QPointF()
            : m_correction.correctionFor(tabletEvent->xTilt(), tabletEvent->yTilt());

        m_hasLastTabletEvent = !shift.isNull();
        if (m_hasLastTabletEvent) {
            m_lastRawGlobalPos = globalPosOf(tabletEvent);
            m_lastShift = shift;
            m_lastTimestamp = tabletEvent->timestamp();

            TabletEventPositionShifter::shift(tabletEvent, shift);
        }
    } else if (m_hasLastTabletEvent) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

        const QPointF distance = globalPosOf(mouseEvent) - m_lastRawGlobalPos;
        const ulong delay = mouseEvent->timestamp() > m_lastTimestamp
            ? mouseEvent->timestamp() - m_lastTimestamp
            : m_lastTimestamp - mouseEvent->timestamp();

        // a mouse event synthesized from the (unaccepted) tablet event
        if (qAbs(distance.x()) <= maxSynthesizedMouseDistance &&
            qAbs(distance.y()) <= maxSynthesizedMouseDistance &&
            delay <= maxSynthesizedMouseDelay) {

            MouseEventPositionShifter::shift(mouseEvent, m_lastShift);
        }
    }

    return false;
}
