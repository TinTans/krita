/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTabletPositionCorrection.h"

#include <QtMath>

#include <kis_config.h>

namespace {

// Minimal spread of the tilt vector (its smallest standard deviation over
// the samples) needed to estimate the tilt matrix. sin(10deg) ~= 0.17
const qreal minTiltSpread = 0.15;

/**
 * Solves the 3x3 linear system A * x = b using Cramer's rule.
 * Returns false if the system is (nearly) singular.
 */
bool solve3x3(const qreal a[3][3], const qreal b[3], qreal x[3])
{
    auto det3 = [](const qreal m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };

    const qreal det = det3(a);
    if (qAbs(det) < 1e-9) return false;

    for (int col = 0; col < 3; col++) {
        qreal m[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                m[i][j] = (j == col) ? b[i] : a[i][j];
            }
        }
        x[col] = det3(m) / det;
    }

    return true;
}

}

KisTabletPositionCorrection KisTabletPositionCorrection::fromConfig(bool defaultValue)
{
    KisConfig cfg(true);

    KisTabletPositionCorrection result;
    result.m_offset = cfg.tabletPositionOffset(defaultValue);

    if (!defaultValue) {
        result.m_tiltEnabled = cfg.readEntry("tabletTiltCompensation", false);
        result.m_m11 = cfg.readEntry("tabletTiltMatrix11", 0.0);
        result.m_m12 = cfg.readEntry("tabletTiltMatrix12", 0.0);
        result.m_m21 = cfg.readEntry("tabletTiltMatrix21", 0.0);
        result.m_m22 = cfg.readEntry("tabletTiltMatrix22", 0.0);
    }

    return result;
}

void KisTabletPositionCorrection::saveToConfig(KisConfig &cfg) const
{
    cfg.setTabletPositionOffset(m_offset);
    cfg.writeEntry("tabletTiltCompensation", m_tiltEnabled);
    cfg.writeEntry("tabletTiltMatrix11", m_m11);
    cfg.writeEntry("tabletTiltMatrix12", m_m12);
    cfg.writeEntry("tabletTiltMatrix21", m_m21);
    cfg.writeEntry("tabletTiltMatrix22", m_m22);
}

QPointF KisTabletPositionCorrection::tiltVector(qreal xTilt, qreal yTilt)
{
    /**
     * Treat (xTilt, yTilt) as the direction the pen leans in, with its
     * length being the angle between the pen and the screen normal (this
     * is how e.g. Android reports it). For the planar angles used by other
     * platforms this is a close approximation in the usable range of tilt,
     * and the fitted matrix absorbs the remaining difference in scale.
     */
    const qreal angle = qSqrt(xTilt * xTilt + yTilt * yTilt);
    if (angle < 1e-3) return QPointF();

    const qreal projection = qSin(qDegreesToRadians(qMin(angle, qreal(90.0))));
    return QPointF(xTilt, yTilt) * (projection / angle);
}

QPointF KisTabletPositionCorrection::correctionFor(qreal xTilt, qreal yTilt) const
{
    QPointF result = m_offset;

    if (m_tiltEnabled) {
        const QPointF t = tiltVector(xTilt, yTilt);
        result += QPointF(m_m11 * t.x() + m_m12 * t.y(),
                          m_m21 * t.x() + m_m22 * t.y());
    }

    return result;
}

bool KisTabletPositionCorrection::hasTiltModel() const
{
    return !qFuzzyIsNull(m_m11) || !qFuzzyIsNull(m_m12) ||
           !qFuzzyIsNull(m_m21) || !qFuzzyIsNull(m_m22);
}

bool KisTabletPositionCorrection::isNull() const
{
    return m_offset.isNull() && !(m_tiltEnabled && hasTiltModel());
}

qreal KisTabletPositionCorrection::sensorDistance() const
{
    // for M = -d * I this gives exactly d
    return qSqrt((m_m11 * m_m11 + m_m12 * m_m12 + m_m21 * m_m21 + m_m22 * m_m22) / 2.0);
}

void KisTabletPositionCorrection::resetTiltModel()
{
    m_tiltEnabled = false;
    m_m11 = m_m12 = m_m21 = m_m22 = 0.0;
}

KisTabletPositionCorrection KisTabletPositionCorrection::fit(const QVector<Sample> &samples, bool *tiltFitted)
{
    KisTabletPositionCorrection result;
    if (tiltFitted) *tiltFitted = false;

    const int n = samples.size();
    if (n == 0) return result;

    QVector<QPointF> tilts;
    QPointF meanTilt;
    QPointF meanDelta;
    Q_FOREACH (const Sample &s, samples) {
        const QPointF t = tiltVector(s.xTilt, s.yTilt);
        tilts.append(t);
        meanTilt += t;
        meanDelta += s.delta;
    }
    meanTilt /= n;
    meanDelta /= n;

    // the constant-only model is the fallback
    result.m_offset = meanDelta;

    // check that the tilt varied enough in both directions: the smallest
    // eigenvalue of the covariance matrix of the tilt vectors
    qreal cxx = 0, cxy = 0, cyy = 0;
    Q_FOREACH (const QPointF &t, tilts) {
        const QPointF d = t - meanTilt;
        cxx += d.x() * d.x();
        cxy += d.x() * d.y();
        cyy += d.y() * d.y();
    }
    cxx /= n; cxy /= n; cyy /= n;

    const qreal trace = cxx + cyy;
    const qreal diff = cxx - cyy;
    const qreal minEigenvalue = 0.5 * (trace - qSqrt(diff * diff + 4 * cxy * cxy));

    if (n < 4 || minEigenvalue < minTiltSpread * minTiltSpread) {
        return result;
    }

    // least squares for delta = c + M * t, separately for x and y:
    // normal equations with the design row [1, tx, ty]
    qreal ata[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    qreal atbx[3] = {0, 0, 0};
    qreal atby[3] = {0, 0, 0};

    for (int i = 0; i < n; i++) {
        const qreal row[3] = {1.0, tilts[i].x(), tilts[i].y()};
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                ata[j][k] += row[j] * row[k];
            }
            atbx[j] += row[j] * samples[i].delta.x();
            atby[j] += row[j] * samples[i].delta.y();
        }
    }

    qreal px[3];
    qreal py[3];
    if (!solve3x3(ata, atbx, px) || !solve3x3(ata, atby, py)) {
        return result;
    }

    result.m_offset = QPointF(px[0], py[0]);
    result.m_m11 = px[1];
    result.m_m12 = px[2];
    result.m_m21 = py[1];
    result.m_m22 = py[2];
    result.m_tiltEnabled = true;

    if (tiltFitted) *tiltFitted = true;

    return result;
}
