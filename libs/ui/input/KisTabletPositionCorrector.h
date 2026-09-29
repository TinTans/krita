/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTABLETPOSITIONCORRECTOR_H
#define KISTABLETPOSITIONCORRECTOR_H

#include <QObject>
#include <QPointF>

#include "KisTabletPositionCorrection.h"
#include "kritaui_export.h"

/**
 * Application-wide event filter that applies the user's
 * KisTabletPositionCorrection to all stylus input: the canvas as well as
 * the rest of the user interface.
 *
 * Tablet events are shifted when they arrive at their top-level window,
 * before Qt picks the widget under the stylus. When a widget ignores
 * the tablet event, Qt synthesizes a mouse event from the *raw* position
 * of the tablet event; such mouse events are recognized (same position
 * and time as the last tablet event) and shifted by the same amount.
 *
 * All events are modified in-place, so they keep their "spontaneous"
 * flag, and Qt's own handling (double-clicks, grabs, hover) stays intact.
 */
class KRITAUI_EXPORT KisTabletPositionCorrector : public QObject
{
    Q_OBJECT
public:
    /// creates the corrector and installs it on the application, if needed
    static KisTabletPositionCorrector *instance();

    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * Temporarily disables the correction, e.g. while calibrating (which
     * needs the raw positions) or in the Tablet Tester (which applies the
     * correction being edited itself).
     */
    class KRITAUI_EXPORT SuspendGuard
    {
    public:
        SuspendGuard();
        ~SuspendGuard();
    private:
        Q_DISABLE_COPY(SuspendGuard)
    };

private Q_SLOTS:
    void slotConfigChanged();

private:
    KisTabletPositionCorrector();

    void updateCorrectionForOrientation();

private:
    KisTabletPositionCorrection m_correction;
    int m_correctionOrientation {-1};
    int m_suspendCount {0};

    // the last tablet event, to recognize mouse events synthesized from it
    bool m_hasLastTabletEvent {false};
    QPointF m_lastRawGlobalPos;
    QPointF m_lastShift;
    ulong m_lastTimestamp {0};
};

#endif // KISTABLETPOSITIONCORRECTOR_H
