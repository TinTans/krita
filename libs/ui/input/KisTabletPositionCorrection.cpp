/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTabletPositionCorrection.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtMath>

#include <klocalizedstring.h>
#include <kis_config.h>

namespace {

// Minimal spread of the tilt vector (standard deviation, in units of
// sin(tilt)) needed to measure the tilt model. sin(10deg) ~= 0.17
const qreal minTiltSpread = 0.15;

/**
 * Solves the NxN linear system A * x = b with Gaussian elimination and
 * partial pivoting. Returns false if the system is (nearly) singular.
 */
template <int N>
bool solveLinear(qreal a[N][N], qreal b[N], qreal x[N])
{
    for (int col = 0; col < N; col++) {
        int pivot = col;
        for (int row = col + 1; row < N; row++) {
            if (qAbs(a[row][col]) > qAbs(a[pivot][col])) pivot = row;
        }
        if (qAbs(a[pivot][col]) < 1e-9) return false;

        if (pivot != col) {
            for (int k = 0; k < N; k++) std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }

        for (int row = col + 1; row < N; row++) {
            const qreal factor = a[row][col] / a[col][col];
            for (int k = col; k < N; k++) a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }

    for (int row = N - 1; row >= 0; row--) {
        qreal sum = b[row];
        for (int k = row + 1; k < N; k++) sum -= a[row][k] * x[k];
        x[row] = sum / a[row][row];
    }

    return true;
}

QString orientationSuffix(int screenOrientation)
{
    return QString("_o%1").arg(screenOrientation);
}

}

int KisTabletPositionCorrection::currentScreenOrientation()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? int(screen->primaryOrientation()) : int(Qt::PrimaryOrientation);
}

QString KisTabletPositionCorrection::screenOrientationName(int screenOrientation)
{
    switch (screenOrientation) {
    case Qt::LandscapeOrientation:
        return i18nc("screen orientation", "landscape");
    case Qt::PortraitOrientation:
        return i18nc("screen orientation", "portrait");
    case Qt::InvertedLandscapeOrientation:
        return i18nc("screen orientation", "upside-down landscape");
    case Qt::InvertedPortraitOrientation:
        return i18nc("screen orientation", "upside-down portrait");
    default:
        return i18nc("screen orientation", "default");
    }
}

KisTabletPositionCorrection KisTabletPositionCorrection::fromConfig(bool defaultValue, int screenOrientation)
{
    KisTabletPositionCorrection result;
    if (defaultValue) return result;

    KisConfig cfg(true);

    if (screenOrientation < 0) {
        screenOrientation = currentScreenOrientation();
    }

    // use the settings of this orientation if it was ever set up,
    // otherwise the most recently saved ones
    QString suffix = orientationSuffix(screenOrientation);
    if (!cfg.readEntry("tabletCorrectionSaved" + suffix, false)) {
        suffix.clear();
    }

    if (suffix.isEmpty()) {
        result.m_offset = cfg.tabletPositionOffset();
    } else {
        result.m_offset = QPointF(cfg.readEntry("tabletPositionOffsetX" + suffix, 0.0),
                                  cfg.readEntry("tabletPositionOffsetY" + suffix, 0.0));
    }

    result.m_tiltEnabled = cfg.readEntry("tabletTiltCompensation" + suffix, false);
    result.m_m11 = cfg.readEntry("tabletTiltMatrix11" + suffix, 0.0);
    result.m_m12 = cfg.readEntry("tabletTiltMatrix12" + suffix, 0.0);
    result.m_m21 = cfg.readEntry("tabletTiltMatrix21" + suffix, 0.0);
    result.m_m22 = cfg.readEntry("tabletTiltMatrix22" + suffix, 0.0);

    return result;
}

void KisTabletPositionCorrection::saveToConfig(KisConfig &cfg, int screenOrientation) const
{
    if (screenOrientation < 0) {
        screenOrientation = currentScreenOrientation();
    }

    // the unsuffixed keys hold the fallback for uncalibrated orientations
    const QStringList suffixes = {QString(), orientationSuffix(screenOrientation)};
    Q_FOREACH (const QString &suffix, suffixes) {
        if (suffix.isEmpty()) {
            cfg.setTabletPositionOffset(m_offset);
        } else {
            cfg.writeEntry("tabletPositionOffsetX" + suffix, m_offset.x());
            cfg.writeEntry("tabletPositionOffsetY" + suffix, m_offset.y());
            cfg.writeEntry("tabletCorrectionSaved" + suffix, true);
        }
        cfg.writeEntry("tabletTiltCompensation" + suffix, m_tiltEnabled);
        cfg.writeEntry("tabletTiltMatrix11" + suffix, m_m11);
        cfg.writeEntry("tabletTiltMatrix12" + suffix, m_m12);
        cfg.writeEntry("tabletTiltMatrix21" + suffix, m_m21);
        cfg.writeEntry("tabletTiltMatrix22" + suffix, m_m22);
    }
}

QPointF KisTabletPositionCorrection::tiltVector(qreal xTilt, qreal yTilt)
{
    /**
     * Treat (xTilt, yTilt) as the direction the pen leans in, with its
     * length being the angle between the pen and the screen normal (this
     * is how e.g. Android reports it). For the planar angles used by other
     * platforms this is a close approximation in the usable range of tilt,
     * and the fitted sensor distance absorbs the remaining difference.
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
    // for M = d * R(theta) this gives exactly d
    return qSqrt((m_m11 * m_m11 + m_m12 * m_m12 + m_m21 * m_m21 + m_m22 * m_m22) / 2.0);
}

void KisTabletPositionCorrection::resetTiltModel()
{
    m_tiltEnabled = false;
    m_m11 = m_m12 = m_m21 = m_m22 = 0.0;
}

KisTabletPositionCorrection KisTabletPositionCorrection::fit(const QVector<Sample> &samples, FitInfo *info)
{
    KisTabletPositionCorrection result;
    FitInfo localInfo;

    const int n = samples.size();
    if (n == 0) {
        if (info) *info = localInfo;
        return result;
    }

    QVector<QPointF> tilts;
    QPointF meanTilt;
    QPointF meanDelta;
    localInfo.minTilt = localInfo.maxTilt = QPointF(samples[0].xTilt, samples[0].yTilt);

    Q_FOREACH (const Sample &s, samples) {
        const QPointF t = tiltVector(s.xTilt, s.yTilt);
        tilts.append(t);
        meanTilt += t;
        meanDelta += s.delta;

        localInfo.minTilt.rx() = qMin(localInfo.minTilt.x(), s.xTilt);
        localInfo.minTilt.ry() = qMin(localInfo.minTilt.y(), s.yTilt);
        localInfo.maxTilt.rx() = qMax(localInfo.maxTilt.x(), s.xTilt);
        localInfo.maxTilt.ry() = qMax(localInfo.maxTilt.y(), s.yTilt);
    }
    meanTilt /= n;
    meanDelta /= n;

    auto rmsErrorOf = [&samples](const KisTabletPositionCorrection &model) {
        qreal sum = 0;
        Q_FOREACH (const Sample &s, samples) {
            const QPointF err = s.delta - model.correctionFor(s.xTilt, s.yTilt);
            sum += err.x() * err.x() + err.y() * err.y();
        }
        return qSqrt(sum / samples.size());
    };

    // the constant-only model is the fallback
    result.m_offset = meanDelta;
    localInfo.rmsError = rmsErrorOf(result);

    // total spread of the tilt vectors
    qreal variance = 0;
    Q_FOREACH (const QPointF &t, tilts) {
        const QPointF d = t - meanTilt;
        variance += d.x() * d.x() + d.y() * d.y();
    }
    variance /= n;

    if (n < 3 || variance < minTiltSpread * minTiltSpread) {
        if (info) *info = localInfo;
        return result;
    }

    /**
     * Least squares for delta = c + M * t with M = [a -s*b; b s*a], where
     * s = 1 is a rotation+scale and s = -1 is a mirrored one. The unknowns
     * are p = (cx, cy, a, b), and every sample gives two rows:
     *
     *     dx = cx + a * tx - s * b * ty
     *     dy = cy + b * tx + s * a * ty
     *
     * Both variants are fitted and the one with the smaller error wins.
     * If the tilt varied only along one direction they fit equally well;
     * the non-mirrored one is then preferred.
     */
    KisTabletPositionCorrection best;
    qreal bestError = -1;
    bool bestMirrored = false;

    for (int s : {1, -1}) {
        qreal ata[4][4] = {};
        qreal atb[4] = {};

        for (int i = 0; i < n; i++) {
            const qreal tx = tilts[i].x();
            const qreal ty = tilts[i].y();
            const qreal rowX[4] = {1.0, 0.0, tx, -s * ty};
            const qreal rowY[4] = {0.0, 1.0, s * ty, tx};

            for (int j = 0; j < 4; j++) {
                for (int k = 0; k < 4; k++) {
                    ata[j][k] += rowX[j] * rowX[k] + rowY[j] * rowY[k];
                }
                atb[j] += rowX[j] * samples[i].delta.x() + rowY[j] * samples[i].delta.y();
            }
        }

        qreal p[4];
        if (!solveLinear<4>(ata, atb, p)) continue;

        KisTabletPositionCorrection model;
        model.m_offset = QPointF(p[0], p[1]);
        model.m_m11 = p[2];
        model.m_m12 = -s * p[3];
        model.m_m21 = p[3];
        model.m_m22 = s * p[2];
        model.m_tiltEnabled = true;

        const qreal error = rmsErrorOf(model);

        // require a clear improvement to pick the mirrored variant
        if (bestError < 0 || error < bestError * 0.8) {
            best = model;
            bestError = error;
            bestMirrored = (s < 0);
        }
    }

    if (bestError < 0) {
        if (info) *info = localInfo;
        return result;
    }

    localInfo.tiltFitted = true;
    localInfo.mirrored = bestMirrored;
    localInfo.rmsError = bestError;

    if (info) *info = localInfo;
    return best;
}
