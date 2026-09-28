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

#include "kritaui_export.h"

class QPushButton;

/**
 * A full-screen dialog that asks the user to tap a series of crosshairs
 * with the stylus and computes the average difference between where the
 * crosshairs are and where the tablet reported the taps. The result can
 * be used as KisConfig::tabletPositionOffset().
 *
 * The dialog receives raw (uncorrected) tablet events, so the currently
 * configured offset doesn't affect the measurement.
 */
class KRITAUI_EXPORT KisDlgTabletOffsetCalibration : public QDialog
{
    Q_OBJECT

public:
    KisDlgTabletOffsetCalibration(QWidget *parent = nullptr);
    ~KisDlgTabletOffsetCalibration() override;

    /// the measured offset, valid only after the dialog was accepted
    QPointF offset() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void slotRestart();

private:
    QPointF targetPosition(int index) const;
    void updateButtonsGeometry();

private:
    QVector<QPointF> m_relativeTargets;
    QVector<QPointF> m_measuredDeltas;
    QPointF m_offset;
    QString m_message;

    QPushButton *m_btnRestart {nullptr};
    QPushButton *m_btnCancel {nullptr};
};

#endif // KISDLGTABLETOFFSETCALIBRATION_H
