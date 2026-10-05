#include "SaturnAssetsTab.h"

#include "RunPanel.h"
#include "Worker.h"
#include "core/Migrate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QWidget* pathRow(QLineEdit* edit, QPushButton* browse) {
    auto* w = new QWidget();
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit, 1);
    l->addWidget(browse);
    return w;
}

}  // namespace

SaturnAssetsTab::SaturnAssetsTab(QWidget* parent) : QWidget(parent) {
    m_image = new QLineEdit(this);
    auto* browseImage = new QPushButton(tr("Browse..."), this);

    m_target = new QLineEdit(this);
    m_target->setText(QCoreApplication::applicationDirPath());
    auto* browseTarget = new QPushButton(tr("Browse..."), this);

    m_models = new QCheckBox(
        tr("Saturn-only creatures (the Tick, Zombie Wesker) -> PC models"), this);
    m_models->setChecked(true);
    m_models->setToolTip(
        tr("Converts ENEMY/EM1016, EM1116 (the Tick) and EM1017, EM1117 "
           "(Zombie Wesker) into PC .EMD files in SATURN/Enemy.\n"
           "The mesh and textures are the Saturn's own; the animations are the "
           "Hunter's and the zombie's, which the Saturn models share.\n"
           "The textures need more than one texture page, which the game "
           "supports for these models only."));
    m_rooms = new QCheckBox(tr("Battle Game arena rooms (STAGE8)"), this);
    m_rooms->setChecked(true);
    m_rooms->setToolTip(
        tr("Decodes the 15 arena rooms STAGE8/ROOM8010-80F0.RDT into "
           "SATURN/Stage8. Decoded, they are ordinary PS1 rooms; their "
           "backgrounds are the campaign rooms' own, so none are copied."));
    m_backgrounds = new QCheckBox(
        tr("Laboratory Tyrant room backgrounds (the room Chris' second Tyrant "
           "is in)"),
        this);
    m_backgrounds->setChecked(true);
    m_backgrounds->setToolTip(
        tr("Converts STAGE5/ROOM513.BSS into SATURN/Stage5/RC5130..5137.pak. The "
           "Saturn re-drew that room on all eight cameras - the tank vats are "
           "emptied, because the Saturn cannot draw the PC's translucent "
           "glass, and the second capsule Chris' second Tyrant comes out of is "
           "in this room.\n"
           "Rooms whose art the Saturn did not change are left to the base "
           "tree."));
    m_music = new QCheckBox(tr("Battle Game music (rendered from the Saturn's sequences)"), this);
    m_music->setChecked(true);
    m_music->setToolTip(
            tr("Renders the Battle Game's own tracks - the arena theme and end-of-run "
               "fanfare (SND/BATTLE.CDP), the arena 15 Tyrant theme (BGM34B.CDP), "
               "and the ranking theme (SND/NAME.CDP) - into SATURN/Sound as "
               "SAT_BATTLE, SAT_BATTLE_END, SAT_BATTLE_TYRANT and SAT_RANKING.\n"
           "The Saturn plays its music as sequences on its sound chip; they are "
           "played here through a model of Sega's sound driver and the SCSP, "
           "reverb included. The other arenas reuse campaign music the PC "
           "already has."));
    m_tickSfx = new QCheckBox(tr("Tick sound effects (rendered from the Saturn's SE bank)"), this);
    m_tickSfx->setChecked(true);
    m_tickSfx->setToolTip(
        tr("The Tick has its own sounds on the Saturn: SND/SE308.CDP (the main "
           "hall bank, where the Hunters are Ticks) - footsteps, jump, attack, "
           "landing, smash, damage, death and the arm striking the floor - "
           "rendered mono into SATURN/Sound as SAT_TICK_*. The songs are the "
           "ones the bank's own slot table names (id - 768), not songs 0-8; the "
           "PC gives the Hunter He_walkA .. He_Nout in the first eight slots."));
    m_musicFormat = new QComboBox(this);
    m_musicFormat->addItem(tr("WAV (lossless, ~12 MB)"), false);
    m_musicFormat->addItem(tr("Ogg Vorbis (needs ffmpeg)"), true);
    m_musicQuality = new QSpinBox(this);
    m_musicQuality->setRange(0, 10);
    m_musicQuality->setValue(6);
    m_musicQuality->setToolTip(tr("Vorbis quality (-q:a)."));
    m_ffmpeg = new QLineEdit(this);
    m_ffmpeg->setText(QStringLiteral("ffmpeg"));
    auto* musicRow = new QWidget(this);
    {
        auto* l = new QHBoxLayout(musicRow);
        l->setContentsMargins(24, 0, 0, 0);
        l->addWidget(new QLabel(tr("Format:"), musicRow));
        l->addWidget(m_musicFormat);
        l->addWidget(new QLabel(tr("Quality:"), musicRow));
        l->addWidget(m_musicQuality);
        l->addWidget(new QLabel(tr("ffmpeg:"), musicRow));
        l->addWidget(m_ffmpeg, 1);
    }
    auto syncMusic = [this]() {
        const bool on = m_music->isChecked() || m_tickSfx->isChecked();
        const bool ogg = m_musicFormat->currentData().toBool();
        m_musicFormat->setEnabled(on);
        m_musicQuality->setEnabled(on && ogg);
        m_ffmpeg->setEnabled(on && ogg);
    };
    connect(m_music, &QCheckBox::toggled, this, syncMusic);
    connect(m_tickSfx, &QCheckBox::toggled, this, syncMusic);
    connect(m_musicFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncMusic);
    syncMusic();
    m_verify = new QCheckBox(tr("Verify the overlay after building"), this);
    m_verify->setChecked(true);
    auto* form = new QFormLayout();
    form->addRow(tr("Disc image:"), pathRow(m_image, browseImage));
    form->addRow(tr("Target game folder:"), pathRow(m_target, browseTarget));
    form->addRow(QString(), m_models);
    form->addRow(QString(), m_rooms);
    form->addRow(QString(), m_backgrounds);
    form->addRow(QString(), m_music);
    form->addRow(QString(), m_tickSfx);
    form->addRow(QString(), musicRow);
    form->addRow(QString(), m_verify);

    m_run = new RunPanel(this);

    auto* note = new QLabel(
            tr("Builds the Saturn overlay (<target>/SATURN) from a Resident Evil "
               "Sega Saturn disc image: the track-1 .bin or its .cue. Only the "
               "content the Saturn release adds is written; everything else falls "
               "back to the base tree, which is never written to.\n\n"
               "Not migrated yet: the rest of the Saturn audio (its campaign "
               "music and sound effects), the FMV (MOVIE.CPK), the title "
               "and ending art, and the extra lab Tyrant capsule itself (its A_TRS1 "
               "model). The game port selects this overlay when [Game] Mode=SATURN "
               "is enabled."),
        this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(m_run, 1);

    connect(browseImage, &QPushButton::clicked, this, &SaturnAssetsTab::onBrowseImage);
    connect(browseTarget, &QPushButton::clicked, this, &SaturnAssetsTab::onBrowseTarget);

    m_run->setTaskFactory([this]() -> Worker* {
        re1::SaturnMigrationOptions opts;
        opts.imagePath = m_image->text().toStdString();
        opts.targetRoot = m_target->text().toStdString();
        opts.convertModels = m_models->isChecked();
        opts.arenaRooms = m_rooms->isChecked();
        opts.roomBackgrounds = m_backgrounds->isChecked();
        opts.battleMusic = m_music->isChecked();
        opts.tickSfx = m_tickSfx->isChecked();
        opts.musicFormat = m_musicFormat->currentData().toBool() ? re1::AudioFormat::Ogg
                                                                 : re1::AudioFormat::Wav;
        opts.oggQuality = m_musicQuality->value();
        opts.ffmpegPath = m_ffmpeg->text().toStdString();
        opts.verify = m_verify->isChecked();
        return new Worker(
            [opts](const re1::Progress& p, QString& error) {
                std::string err;
                const bool ok = re1::migrateSaturnAssets(opts, p, &err);
                if (!ok) error = QString::fromStdString(err);
                return ok;
            });
    });
}

void SaturnAssetsTab::onBrowseImage() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Select the Saturn disc image (track 1)"), m_image->text(),
        tr("Disc images (*.bin *.cue *.iso);;All files (*)"));
    if (!f.isEmpty()) m_image->setText(f);
}

void SaturnAssetsTab::onBrowseTarget() {
    const QString d = QFileDialog::getExistingDirectory(
        this, tr("Select the game folder"), m_target->text());
    if (!d.isEmpty()) m_target->setText(d);
}
