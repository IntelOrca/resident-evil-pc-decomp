#pragma once
// "Sega Saturn" tab: build the SATURN overlay from a Saturn disc image.

#include <QWidget>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLineEdit;
class RunPanel;

class SaturnAssetsTab : public QWidget {
    Q_OBJECT
public:
    explicit SaturnAssetsTab(QWidget* parent = nullptr);

private slots:
    void onBrowseImage();
    void onBrowseTarget();

private:
    QLineEdit* m_image = nullptr;
    QLineEdit* m_target = nullptr;
    QCheckBox* m_allRooms = nullptr;
    QCheckBox* m_allBgm = nullptr;
    QCheckBox* m_movies = nullptr;
    QComboBox* m_musicFormat = nullptr;
    QSpinBox* m_musicQuality = nullptr;
    QLineEdit* m_ffmpeg = nullptr;
    QCheckBox* m_verify = nullptr;
    RunPanel* m_run = nullptr;
};
