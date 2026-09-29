/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTABLETPOSITIONCORRECTION_H
#define KISTABLETPOSITIONCORRECTION_H

#include <QPointF>
#include <QVector>

#include "kritaui_export.h"

class KisConfig;

/**
 * Correction applied to the position of stylus events so that the
 * reported point matches the actual position of the nib.
 *
 * The position sensor of a stylus sits some distance up the barrel,
 * so when the pen is tilted, the detected point moves away from the nib
 * towards the top of the pen. The correction is modelled as
 *
 *     correction = offset + M * t
 *
 * where `offset` is a constant shift (parallax, digitizer misalignment)
 * and `t` is the projection of the pen axis onto the screen:
 * sin(tilt angle) in the direction the pen is leaning. For an ideal pen
 * M = -d * I, where d is the distance between the nib and the sensor,
 * but M is a full 2x2 matrix so that it also absorbs whatever sign,
 * axis and scale conventions the platform uses for reporting tilt.
 *
 * All values are in logical pixels.
 */
class KRITAUI_EXPORT KisTabletPositionCorrection
{
public:
    struct Sample {
        QPointF delta;  ///< target position minus the reported position
        qreal xTilt {0.0};
        qreal yTilt {0.0};
    };

public:
    KisTabletPositionCorrection() = default;

    /// loads the correction from Krita's configuration
    static KisTabletPositionCorrection fromConfig(bool defaultValue = false);
    void saveToConfig(KisConfig &cfg) const;

    /**
     * Least-squares fit of the correction to the calibration samples.
     * If the samples don't contain enough variation of tilt (or the
     * stylus doesn't report tilt at all), only the constant offset is
     * fitted and \p tiltFitted is set to false.
     */
    static KisTabletPositionCorrection fit(const QVector<Sample> &samples, bool *tiltFitted = nullptr);

    /// projection of the pen axis onto the screen plane for the given tilt
    static QPointF tiltVector(qreal xTilt, qreal yTilt);

    /// the shift that should be added to a stylus event with the given tilt
    QPointF correctionFor(qreal xTilt, qreal yTilt) const;

    bool isNull() const;

    QPointF offset() const { return m_offset; }
    void setOffset(const QPointF &offset) { m_offset = offset; }

    bool tiltCompensationEnabled() const { return m_tiltEnabled; }
    void setTiltCompensationEnabled(bool value) { m_tiltEnabled = value; }

    /// true if the tilt model has any effect when enabled
    bool hasTiltModel() const;

    /**
     * Estimated distance between the nib and the position sensor, as it
     * would be seen on screen with the pen lying flat (90 degrees tilt).
     */
    qreal sensorDistance() const;

    void resetTiltModel();

private:
    QPointF m_offset;
    bool m_tiltEnabled {false};

    // tilt matrix, correction += [m11 m12; m21 m22] * tiltVector
    qreal m_m11 {0.0};
    qreal m_m12 {0.0};
    qreal m_m21 {0.0};
    qreal m_m22 {0.0};
};

#endif // KISTABLETPOSITIONCORRECTION_H
