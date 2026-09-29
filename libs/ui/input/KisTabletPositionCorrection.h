/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTABLETPOSITIONCORRECTION_H
#define KISTABLETPOSITIONCORRECTION_H

#include <QPointF>
#include <QString>
#include <QVector>

#include "kritaui_export.h"

class KisConfig;

/**
 * Correction applied to the position of stylus events so that the
 * reported point matches the actual position of the nib.
 *
 * The position sensor of a stylus sits some distance d up the barrel,
 * so when the pen is tilted, the detected point moves away from the nib
 * towards the top of the pen by d * sin(tilt angle), in the direction the
 * pen is leaning. The correction is modelled as
 *
 *     correction = offset + M * t
 *
 * where `offset` is a constant shift (parallax, digitizer misalignment)
 * and `t` is the projection of the pen axis onto the screen: sin(tilt
 * angle) in the direction the pen is leaning, as reported by the tilt
 * axes.
 *
 * M = d * R(theta) is the sensor distance combined with a rotation, which
 * absorbs any mismatch between the directions of the platform's tilt axes
 * and the screen axes (possibly with a mirror, if one of the tilt axes is
 * flipped). Because it has only two degrees of freedom it can be measured
 * even if the tilt only varied along a single direction during the
 * calibration, and it then works for any direction of tilt.
 *
 * The correction is stored separately for every screen orientation, with
 * the most recently saved one used for orientations that were never
 * calibrated.
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

    struct FitInfo {
        bool tiltFitted {false};
        bool mirrored {false};
        qreal rmsError {0.0};   ///< remaining error of the fitted model, in pixels
        QPointF minTilt;        ///< range of the reported tilt, in degrees
        QPointF maxTilt;
    };

public:
    KisTabletPositionCorrection() = default;

    /**
     * Loads the correction for the given screen orientation (by default,
     * the current one of the primary screen) from Krita's configuration.
     */
    static KisTabletPositionCorrection fromConfig(bool defaultValue = false, int screenOrientation = -1);

    /**
     * Saves the correction for the given screen orientation (by default,
     * the current one), and as the fallback for uncalibrated orientations.
     */
    void saveToConfig(KisConfig &cfg, int screenOrientation = -1) const;

    /// the current orientation of the primary screen, as Qt::ScreenOrientation
    static int currentScreenOrientation();
    static QString screenOrientationName(int screenOrientation);

    /**
     * Least-squares fit of the correction to the calibration samples.
     * If the tilt barely changed during the calibration (or the stylus
     * doesn't report tilt at all), only the constant offset is fitted.
     */
    static KisTabletPositionCorrection fit(const QVector<Sample> &samples, FitInfo *info = nullptr);

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
