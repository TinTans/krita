/*
 *  SPDX-FileCopyrightText: 2026 Krita contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISDLGTABLETOFFSETCALIBRATION_H
#define KISDLGTABLETOFFSETCALIBRATION_H

#include <QDialog>
#include <QPointF>
#include <QVector>

#include "input/KisTabletPositionCorrection.h"
#include "kritaui_export.h"

class QPushButton;

/**
 * A full-screen dialog that asks the user to tap a series of crosshairs
 * with the stylus, holding it upright and leaning in different directions.
 * From the difference between the crosshairs and the reported taps it
 * fits a KisTabletPositionCorrection: a constant offset plus a tilt model
 * describing how far the position sensor sits from the nib.
 *
 * The dialog receives raw (uncorrected) tablet events, so the currently
 * configured correction doesn't affect the measurement.
 */
class KRITAUI_EXPORT KisDlgTabletOffsetCalibration : public QDialog
{
    Q_OBJECT

public:
    KisDlgTabletOffsetCalibration(QWidget *parent = nullptr);
    ~KisDlgTabletOffsetCalibration() override;

    /// the measured correction, valid only after the dialog was accepted
    KisTabletPositionCorrection correction() const;

    /// true if the stylus reported enough tilt to measure the tilt model
    bool tiltMeasured() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void slotRestart();

private:
    struct Step {
        QPointF relativeTarget;
        QString instruction;
        bool requiresLean;
    };

    QPointF targetPosition(int index) const;
    void updateButtonsGeometry();

private:
    QVector<Step> m_steps;
    QVector<KisTabletPositionCorrection::Sample> m_samples;
    KisTabletPositionCorrection m_correction;
    bool m_tiltMeasured {false};
    bool m_tiltReported {false};
    QPointF m_currentTilt;
    QString m_message;

    QPushButton *m_btnRestart {nullptr};
    QPushButton *m_btnCancel {nullptr};
};

#endif // KISDLGTABLETOFFSETCALIBRATION_H
