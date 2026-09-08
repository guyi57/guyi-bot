// 
// Shijima-Qt - Cross-platform shimeji simulation app for desktop
// Copyright (C) 2025 pixelomer
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 

#include "ShijimaManager.hpp"
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <QThread>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QMenuBar>
#include <QFileDialog>
#include <QPushButton>
#include <QWindow>
#include <QTextStream>
#include <QGuiApplication>
#include <QFile>
#include <QDesktopServices>
#include <QScreen>
#include <QRandomGenerator>
#include "PlatformWidget.hpp"
#include "ShijimaLicensesDialog.hpp"
#include "ShijimaApiDialog.hpp"
#include "SettingsDb.hpp"
#include "ShijimaWidget.hpp"
#include "HotkeyManager.hpp"
#include "AgentService.hpp"
#include "BehaviorEngine.hpp"
#include "MusicPlayerDialog.hpp"
#include "MusicPlayerManager.hpp"
#include <QDirIterator>
#include <QDesktopServices>
#include <shijima/mascot/factory.hpp>
#include <shimejifinder/analyze.hpp>
#include <QStandardPaths>
#include "ForcedProgressDialog.hpp"
#include <QAbstractItemModel>
#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QListWidget>
#include <QMessageBox>
#include <QUrl>
#include <QtConcurrent>
#include <string>
#include <QLabel>
#include <QFormLayout>
#include <QColorDialog>
#include <cstring>
#include <cstdint>

#define SHIJIMAQT_SUBTICK_COUNT 4

using namespace shijima;

static QString colorToString(QColor const& color) {
    auto rgb = color.toRgb();
    std::array<char, 8> buf;
    snprintf(&buf[0], buf.size(), "#%02hhX%02hhX%02hhX",
        (uint8_t)rgb.red(), (uint8_t)rgb.green(),
        (uint8_t)rgb.blue());
    buf[buf.size()-1] = 0;
    return QString { &buf[0] };
}

// https://stackoverflow.com/questions/34135624/-/54029758#54029758
static void dispatchToMainThread(std::function<void()> callback) {
    QTimer *timer = new QTimer;
    timer->moveToThread(qApp->thread());
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [timer, callback]() {
        callback();
        timer->deleteLater();
    });
    QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
}

static ShijimaManager *m_defaultManager = nullptr;

ShijimaManager *ShijimaManager::defaultManager() {
    if (m_defaultManager == nullptr) {
        m_defaultManager = new ShijimaManager;
    }
    return m_defaultManager;
}

void ShijimaManager::finalize() {
    if (m_defaultManager != nullptr) {
        delete m_defaultManager;
        m_defaultManager = nullptr;
    }
}

void ShijimaManager::killAll() {
    for (auto mascot : m_mascots) {
        mascot->markForDeletion();
    }
}

void ShijimaManager::killAll(QString const& name) {
    for (auto mascot : m_mascots) {
        if (mascot->mascotName() == name) {
            mascot->markForDeletion();
        }
    }
}

void ShijimaManager::killAllButOne(ShijimaWidget *widget) {
    for (auto mascot : m_mascots) {
        if (widget == mascot) {
            continue;
        }
        mascot->markForDeletion();
    }
}

void ShijimaManager::killAllButOne(QString const& name) {
    bool foundOne = false;
    for (auto mascot : m_mascots) {
        if (mascot->mascotName() == name) {
            if (!foundOne) {
                foundOne = true;
                continue;
            }
            mascot->markForDeletion();
        }
    }
}

void ShijimaManager::loadData(MascotData *data) {
    if (data != nullptr && data->valid()) {
        shijima::mascot::factory::tmpl tmpl;
        tmpl.actions_xml = data->actionsXML().toStdString();
        tmpl.behaviors_xml = data->behaviorsXML().toStdString();
        tmpl.name = data->name().toStdString();
        tmpl.path = data->path().toStdString();
        m_factory.register_template(tmpl);
        m_loadedMascots.insert(data->name(), data);
        m_loadedMascotsById.insert(data->id(), data);
        std::cout << "Loaded mascot: " << data->name().toStdString() << std::endl;
    }
    else {
        throw std::runtime_error("loadData() called with invalid data");
    }
}

void ShijimaManager::loadDefaultMascot() {
    auto data = new MascotData { "@", m_idCounter++ };
    loadData(data);
}

QMap<QString, MascotData *> const& ShijimaManager::loadedMascots() {
    return m_loadedMascots;
}

QMap<int, MascotData *> const& ShijimaManager::loadedMascotsById() {
    return m_loadedMascotsById;
}

std::list<ShijimaWidget *> const& ShijimaManager::mascots() {
    return m_mascots;
}

std::map<int, ShijimaWidget *> const& ShijimaManager::mascotsById() {
    return m_mascotsById;
}


void ShijimaManager::reloadMascot(QString const& name) {
    if (m_loadedMascots.contains(name) && !m_loadedMascots[name]->deletable()) {
        std::cout << "Refusing to unload mascot: " << name.toStdString()
            << std::endl;
        return;
    }
    MascotData *data = nullptr;
    try {
        data = new MascotData { m_mascotsPath + QDir::separator() + name + ".mascot",
            m_idCounter++ };
    }
    catch (std::exception &ex) {
        std::cerr << "couldn't load mascot: " << name.toStdString() << std::endl;
        std::cerr << ex.what() << std::endl;
    }
    if (m_loadedMascots.contains(name)) {
        MascotData *data = m_loadedMascots[name];
        m_factory.deregister_template(name.toStdString());
        data->unloadCache();
        killAll(name);
        m_loadedMascots.remove(name);
        m_loadedMascotsById.remove(data->id());
        delete data;
        std::cout << "Unloaded mascot: " << name.toStdString() << std::endl;
    }
    if (data != nullptr) {
        if (data->name() != name) {
            throw std::runtime_error("Impossible condition: New mascot name is incorrect");
        }
        loadData(data);
    }
    m_listItemsToRefresh.insert(name);
}

void ShijimaManager::importAction() {
    auto paths = QFileDialog::getOpenFileNames(this, "选择桌宠压缩包...");
    if (paths.isEmpty()) {
        return;
    }
    importWithDialog(paths);
}

void ShijimaManager::quitAction() {
    m_allowClose = true;
    close();
}

void ShijimaManager::deleteAction() {
    if (m_loadedMascots.size() == 0) {
        return;
    }
    auto selected = m_listWidget.selectedItems();
    for (long i=(long)selected.size()-1; i>=0; --i) {
        auto mascotData = m_loadedMascots[selected[i]->text()];
        if (!mascotData->deletable()) {
            selected.remove(i);
        }
    }
    if (selected.size() == 0) {
        return;
    }
    QString msg = "确定要删除这些桌宠吗？";
    for (long i=0; i<selected.size() && i<5; ++i) {
        msg += "\n* " + selected[i]->text();
    }
    if (selected.size() > 5) {
        msg += "\n... 以及其他 " + QString::number(selected.size() - 5) + " 个";
    }
    QMessageBox msgBox { this };
    msgBox.setWindowTitle("删除桌宠");
    msgBox.setText(msg);
    msgBox.setStandardButtons(QMessageBox::StandardButton::Yes |
        QMessageBox::StandardButton::No);
    msgBox.setIcon(QMessageBox::Icon::Question);
    int ret = msgBox.exec();
    if (ret == QMessageBox::StandardButton::Yes) {
        for (auto item : selected) {
            auto mascotData = m_loadedMascots[item->text()];
            if (!mascotData->deletable()) {
                continue;
            }
            std::filesystem::path path = mascotData->path().toStdString();
            std::cout << "Deleting mascot: " << item->text().toStdString() << std::endl;
            try {
                // remove_all(path) could be dangerous
                std::filesystem::remove_all(path / "img");
                std::filesystem::remove_all(path / "sound");
                std::filesystem::remove(path / "actions.xml");
                std::filesystem::remove(path / "behaviors.xml");
                std::filesystem::remove(path);
            }
            catch (std::exception &ex) {
                std::cerr << "failed to delete mascot: " << path.string()
                    << ": " << ex.what() << std::endl;
            }
            reloadMascot(item->text());
        }
        refreshListWidget();
    }
}

std::unique_lock<std::mutex> ShijimaManager::acquireLock() {
    return std::unique_lock<std::mutex> { m_mutex };
}

void ShijimaManager::updateSandboxBackground() {
    if (m_sandboxWidget != nullptr) {
        m_sandboxWidget->setStyleSheet("#sandboxWindow { background-color: " +
            colorToString(m_sandboxBackground) + "; }");
    }
}

void ShijimaManager::buildToolbar() {
    QAction *action;
    QMenu *menu;
    QMenu *submenu;
    
    menu = menuBar()->addMenu("文件");
    {
        action = menu->addAction("导入桌宠...");
        connect(action, &QAction::triggered, this, &ShijimaManager::importAction);

        action = menu->addAction("打开桌宠文件夹");
        connect(action, &QAction::triggered, [this](){
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_mascotsPath));
        });

        action = menu->addAction("退出");
        connect(action, &QAction::triggered, this, &ShijimaManager::quitAction);
    }

    menu = menuBar()->addMenu("编辑");
    {
        action = menu->addAction("设为默认桌宠 ⭐");
        connect(action, &QAction::triggered, [this]() {
            auto items = m_listWidget.selectedItems();
            if (!items.isEmpty()) {
                QString rawName = items.first()->data(Qt::UserRole).toString();
                if (rawName.isEmpty()) rawName = items.first()->text();
                setDefaultMascot(rawName);
            }
        });

        action = menu->addAction("生成选中桌宠");
        connect(action, &QAction::triggered, [this]() {
            for (auto item : m_listWidget.selectedItems()) {
                itemDoubleClicked(item);
            }
        });

        action = menu->addAction("删除桌宠", QKeySequence::StandardKey::Delete);
        connect(action, &QAction::triggered, this, &ShijimaManager::deleteAction);
    }

    menu = menuBar()->addMenu("设置");
    {
        {
            static const QString key = "multiplicationEnabled";
            bool initial = m_settings.value(key, 
                QVariant::fromValue(true)).toBool();

            action = menu->addAction("启用繁殖");
            action->setCheckable(true);
            action->setChecked(initial);
            for (auto &env : m_env) {
                env->allows_breeding = initial;
            }
            connect(action, &QAction::triggered, [this](bool checked){
                for (auto &env : m_env) {
                    env->allows_breeding = checked;
                }
                m_settings.setValue(key, QVariant::fromValue(checked));
            });
        }

        {
            action = menu->addAction("窗口模式");
            m_windowedModeAction = action;
            action->setCheckable(true);
            action->setChecked(false);
            connect(action, &QAction::triggered, [this](bool checked){
                setWindowedMode(checked);
            });
        }

        {
            static const QString key = "windowedModeBackground";

            QColor initial = m_settings.value(key, "#FF0000").toString();

            action = menu->addAction("窗口模式背景...");
            m_sandboxBackground = initial;
            updateSandboxBackground();
            connect(action, &QAction::triggered, [this](){
                QColorDialog dialog { this };
                dialog.setCurrentColor(m_sandboxBackground);
                int ret = dialog.exec();
                if (ret == 1) {
                    m_sandboxBackground = dialog.selectedColor();
                    m_settings.setValue(key, colorToString(dialog.selectedColor()));
                    updateSandboxBackground();
                }
            });
        }

        submenu = menu->addMenu("缩放");
        {
            static const QString key = "userScale";
            m_userScale = m_settings.value(key,
                QVariant::fromValue(1.0)).toDouble();
            
            auto makeScaleText = [](double scale){
                return QString::asprintf("%.3lfx", scale);
            };

            auto makeCustomActionText = [this, makeScaleText]() {
                return QString { "自定义... (" } +
                    makeScaleText(m_userScale) + ")";
            };
            QAction *customAction = submenu->addAction(makeCustomActionText());

            #define addPreset(scale) do { \
                action = submenu->addAction(#scale "x"); \
                action->setCheckable(true); \
                action->setChecked(std::fabs(m_userScale - scale) < 0.01); \
                connect(action, &QAction::triggered, [this, customAction, \
                    makeCustomActionText, action, submenu]() \
                { \
                    for (auto neighbour : submenu->actions()) { \
                        neighbour->setChecked(false); \
                    } \
                    m_userScale = scale; \
                    m_settings.setValue(key, QVariant::fromValue(scale)); \
                    action->setChecked(true); \
                    customAction->setText(makeCustomActionText()); \
                }); \
            } while (0)
            
            addPreset(0.25);
            addPreset(0.50);
            addPreset(0.75);
            addPreset(1.00);
            addPreset(1.25);
            addPreset(1.50);
            addPreset(1.75);
            addPreset(2.00);

            #undef addPreset

            connect(customAction, &QAction::triggered, [this,
                customAction, makeCustomActionText, submenu, makeScaleText]()
            {
                QDialog dialog { this };
                QFormLayout layout;
                dialog.setLayout(&layout);
                QSlider slider { Qt::Horizontal };
                QLabel label;
                QPushButton button;
                button.setText("保存");
                label.setMinimumWidth(80);
                slider.setMinimumWidth(300);
                layout.addRow(&label, &slider);
                layout.addRow(&button);
                label.setText(makeScaleText(m_userScale));
                slider.setMinimum(100);
                slider.setMaximum(10000);
                slider.setValue(static_cast<int>(m_userScale * 1000.0));
                connect(&slider, &QSlider::valueChanged,
                    [this, &label, makeScaleText](int value)
                {
                    m_userScale = value / 1000.0;
                    label.setText(makeScaleText(m_userScale));
                });
                connect(&button, &QPushButton::clicked,
                    [&dialog]()
                {
                    dialog.close();
                });
                dialog.exec();
                for (auto neighbour : submenu->actions()) {
                    //double value = neighbour->text().sliced(0, 4).toDouble();
                    //std::cout << std::fabs(m_userScale - value) << std::endl;
                    //neighbour->setChecked(std::fabs(m_userScale - value) < 0.01);
                    neighbour->setChecked(false);
                }
                customAction->setText(makeCustomActionText());
                m_settings.setValue(key, QVariant::fromValue(m_userScale));
            });
        }
    }

    menu = menuBar()->addMenu("帮助");
    {
        action = menu->addAction("查看许可证");
        connect(action, &QAction::triggered, [this](){
            ShijimaLicensesDialog dialog { this };
            dialog.exec();
        });

        action = menu->addAction("消息接口介绍");
        connect(action, &QAction::triggered, [this](){
            ShijimaApiDialog dialog { this };
            dialog.exec();
        });

        // action = menu->addAction("访问 Shijima 主页");
        // connect(action, &QAction::triggered, [](){
        //     QDesktopServices::openUrl(QUrl { "https://getshijima.app" });
        // });

        // action = menu->addAction("报告问题");
        // connect(action, &QAction::triggered, [](){
        //     QDesktopServices::openUrl(QUrl { "https://github.com/pixelomer/Shijima-Qt/issues" });
        // });
    }
}

QString ShijimaManager::defaultMascotName() const {
    return SettingsDb::instance()->get("mascot.default_name", "Default Mascot");
}

void ShijimaManager::setDefaultMascot(const QString &name) {
    if (name.trimmed().isEmpty()) return;
    SettingsDb::instance()->set("mascot.default_name", name.trimmed());
    std::cout << "[桌宠管理器] 成功设置默认启动桌宠为: " << name.toStdString() << std::endl;
    refreshListWidget();
}

void ShijimaManager::refreshListWidget() {
    m_listWidget.clear();
    QString defName = defaultMascotName();
    auto names = m_loadedMascots.keys();
    names.sort(Qt::CaseInsensitive);
    for (auto &name : names) {
        auto item = new QListWidgetItem;
        item->setData(Qt::UserRole, name);
        if (name == defName) {
            item->setText(name + " ⭐(默认)");
            item->setToolTip("★ 当前默认启动桌宠: " + name);
        } else {
            item->setText(name);
            item->setToolTip(name);
        }
        item->setIcon(m_loadedMascots[name]->preview());
        m_listWidget.addItem(item);
    }
    m_listItemsToRefresh.clear();
}

void ShijimaManager::loadAllMascots() {
    QDirIterator iter { m_mascotsPath, QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::NoIteratorFlags };
    while (iter.hasNext()) {
        auto name = iter.nextFileInfo().fileName();
        if (!name.endsWith(".mascot") || name.length() <= 7) {
            continue;
        }
        reloadMascot(name.sliced(0, name.length() - 7));
    }
    refreshListWidget();
}

void ShijimaManager::reloadMascots(std::set<std::string> const& mascots) {
    for (auto &mascot : mascots) {
        reloadMascot(QString::fromStdString(mascot));
    }
    refreshListWidget();
}

std::set<std::string> ShijimaManager::import(QString const& path) noexcept {
    try {
        auto ar = shimejifinder::analyze(path.toStdString());
        ar->extract(m_mascotsPath.toStdString());
        return ar->shimejis();
    }
    catch (std::exception &ex) {
        std::cerr << "import failed: " << ex.what() << std::endl;
        return {};
    }
}

void ShijimaManager::importWithDialog(QList<QString> const& paths) {
    ForcedProgressDialog *dialog = new ForcedProgressDialog { this };
    dialog->setRange(0, 0);
    QPushButton *cancelButton = new QPushButton;
    cancelButton->setEnabled(false);
    cancelButton->setText("取消");
    dialog->setModal(true);
    dialog->setCancelButton(cancelButton);
    dialog->setLabelText("正在导入桌宠...");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    //hide();
    QtConcurrent::run([this, paths](){
        std::set<std::string> changed;
        for (auto &path : paths) {
            auto newChanged = import(path);
            changed.insert(newChanged.begin(), newChanged.end());
        }
        return changed;
    }).then([this, dialog](std::set<std::string> changed){
        dispatchToMainThread([this, changed, dialog](){
            reloadMascots(changed);
            this->show();
            dialog->close();
            QString msg;
            QMessageBox::Icon icon;
            if (changed.size() > 0) {
                msg = QString::fromStdString("已导入 " + std::to_string(changed.size()) +
                    " 个桌宠。");
                icon = QMessageBox::Icon::Information;
            }
            else {
                msg = "无法从指定的压缩包中导入任何桌宠。";
                icon = QMessageBox::Icon::Warning;
            }
            QMessageBox msgBox { icon, "导入", msg,
                QMessageBox::StandardButton::Ok, this };
            msgBox.exec();
        });
    });
}

void ShijimaManager::showEvent(QShowEvent *event) {
    PlatformWidget::showEvent(event);
    if (!m_firstShow) {
        return;
    }
    m_firstShow = false;
    if (!m_importOnShowPath.isEmpty()) {
        QString path = m_importOnShowPath;
        m_importOnShowPath = {};
        importWithDialog({ path });
    }
    else {
        if (m_loadedMascots.size() == 1) {
            auto msgBox = new QMessageBox { this };
            msgBox->setText("欢迎使用！你可以拖动桌宠，右键操作桌宠。消息接口是端口是127.0.0.1:32456/guyi/api/v1");
            msgBox->addButton(QMessageBox::StandardButton::Ok);
            msgBox->setAttribute(Qt::WA_DeleteOnClose);
            msgBox->show();
        }
    }
}

void ShijimaManager::importOnShow(QString const& path) {
    m_importOnShowPath = path;
}

void ShijimaManager::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ShijimaManager::dropEvent(QDropEvent *event) {
    QList<QString> paths;
    for (auto &url : event->mimeData()->urls()) {
        paths.append(url.toLocalFile());
    }
    importWithDialog(paths);
}

void ShijimaManager::screenAdded(QScreen *screen) {
    if (!m_env.contains(screen)) {
        auto env = std::make_shared<shijima::mascot::environment>();
        m_env[screen] = env;
        m_reverseEnv[env.get()] = screen;
        auto primary = QGuiApplication::primaryScreen();
        if (screen != primary && m_env.contains(primary)) {
            m_env[screen]->allows_breeding = m_env[primary]->allows_breeding;
        }
    }
}

void ShijimaManager::screenRemoved(QScreen *screen) {
    if (m_env.contains(screen) && screen != nullptr) {
        auto primary = QGuiApplication::primaryScreen();
        for (auto &mascot : m_mascots) {
            mascot->setEnv(m_env[primary]);
            mascot->mascot().reset_position();
        }
        m_reverseEnv.remove(m_env[primary].get());
        m_env.remove(screen);
    }
}

void ShijimaManager::updateGlobalHotkeys() {
    auto cfg = AgentService::instance()->config();
    QString hkTranslate = cfg.hotkeyTranslate.isEmpty() ? "Option+T" : cfg.hotkeyTranslate;
    QString hkAsk = cfg.hotkeyAsk.isEmpty() ? "Option+Q" : cfg.hotkeyAsk;

    HotkeyManager::instance()->registerTranslateHotkey(hkTranslate, [this]() {
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target == nullptr) {
            if (!m_mascots.empty()) target = m_mascots.front();
        }
        if (target != nullptr) {
            QString text = HotkeyManager::instance()->getActiveSelectedText();
            if (text.isEmpty()) {
                target->showMessage("💡 请先划选文字，再按快捷键翻译～", 3500);
            } else {
                target->onTranslateRequested(text);
            }
        }
    });

    HotkeyManager::instance()->registerAskHotkey(hkAsk, [this]() {
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target == nullptr) {
            if (!m_mascots.empty()) target = m_mascots.front();
        }
        if (target != nullptr) {
            QString text = HotkeyManager::instance()->getActiveSelectedText();
            target->onAskRequested(text);
        }
    });

    QString hkHistory = cfg.hotkeyHistory.isEmpty() ? "Option+H" : cfg.hotkeyHistory;
    HotkeyManager::instance()->registerHistoryHotkey(hkHistory, [this]() {
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target == nullptr) {
            if (!m_mascots.empty()) target = m_mascots.front();
        }
        if (target != nullptr) {
            target->showMessageHistory();
        }
    });

    QString hkMusicToggle = cfg.hotkeyMusicToggle.isEmpty() ? "Option+M" : cfg.hotkeyMusicToggle;
    QString hkMusicPlayPause = cfg.hotkeyMusicPlayPause.isEmpty() ? "Option+Space" : cfg.hotkeyMusicPlayPause;
    QString hkMusicNext = cfg.hotkeyMusicNext.isEmpty() ? "Option+Right" : cfg.hotkeyMusicNext;
    QString hkMusicPrev = cfg.hotkeyMusicPrev.isEmpty() ? "Option+Left" : cfg.hotkeyMusicPrev;
    QString hkMusicFav = cfg.hotkeyMusicFav.isEmpty() ? "Option+L" : cfg.hotkeyMusicFav;

    // 音乐播放器全局热键 (打开独立窗口 / 播放暂停 / 下一首 / 上一首 / 一键收藏)
    HotkeyManager::instance()->registerMusicToggleHotkey(hkMusicToggle, []() {
        MusicPlayerDialog::instance()->toggleVisibility();
    });

    HotkeyManager::instance()->registerMusicPlayPauseHotkey(hkMusicPlayPause, []() {
        MusicPlayerManager::instance()->togglePlay();
    });

    HotkeyManager::instance()->registerMusicNextHotkey(hkMusicNext, []() {
        MusicPlayerManager::instance()->playNext();
    });

    HotkeyManager::instance()->registerMusicPrevHotkey(hkMusicPrev, []() {
        MusicPlayerManager::instance()->playPrevious();
    });

    HotkeyManager::instance()->registerMusicFavHotkey(hkMusicFav, []() {
        MusicPlayerManager::instance()->toggleFavoriteCurrent();
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target != nullptr) {
            bool isFav = MusicPlayerManager::instance()->isCurrentSongFavorite();
            auto song = MusicPlayerManager::instance()->currentSong();
            if (!song.name.isEmpty()) {
                target->showMessage(isFav ? QString("❤️ 已收藏《%1》～").arg(song.name) : QString("🤍 已取消收藏《%1》").arg(song.name), 2500);
            }
        }
    });
}

ShijimaManager::~ShijimaManager() {
    HotkeyManager::instance()->unregisterAll();
    disconnect(qApp, &QGuiApplication::screenAdded,
        this, &ShijimaManager::screenAdded);
    disconnect(qApp, &QGuiApplication::screenRemoved,
        this, &ShijimaManager::screenRemoved);
}

void ShijimaManager::onTickSync(std::function<void(ShijimaManager *)> callback) {
    if (QThread::currentThread() == this->thread() || (qApp && QThread::currentThread() == qApp->thread())) {
        callback(this);
        return;
    }
    auto lock = acquireLock();
    m_hasTickCallbacks = true;
    m_tickCallbacks.push_back(callback);
    m_tickCallbackCompletion.wait(lock);
}

void ShijimaManager::onTickAsync(std::function<void(ShijimaManager *)> callback) {
    if (QThread::currentThread() == this->thread() || (qApp && QThread::currentThread() == qApp->thread())) {
        callback(this);
        return;
    }
    QMetaObject::invokeMethod(this, [this, callback]() {
        callback(this);
    }, Qt::QueuedConnection);
}

void ShijimaManager::setWindowedMode(bool windowedMode) {
    if (!!this->windowedMode() == !!windowedMode) {
        // no change
        return;
    }
    m_windowedModeAction->setChecked(windowedMode);
    for (auto mascot : m_mascots) {
        mascot->close();
        mascot->setParent(nullptr);
    }
    if (windowedMode) {
        QWidget *parent;
        #if defined(_WIN32)
            parent = nullptr;
        #else
            parent = this;
        #endif
        m_sandboxWidget = new QWidget { parent, Qt::Window };
        m_sandboxWidget->setAttribute(Qt::WA_StyledBackground, true);
        m_sandboxWidget->resize(640, 480);
        m_sandboxWidget->setObjectName("sandboxWindow");
        m_sandboxWidget->show();
        updateSandboxBackground();
    }
    else {
        m_sandboxWidget->close();
        delete m_sandboxWidget;
        m_sandboxWidget = nullptr;
    }
    updateEnvironment();
    std::shared_ptr<shijima::mascot::environment> env;
    if (windowedMode) {
        env = m_env[nullptr];
    }
    else {
        env = m_env[mascotScreen()];
    }
    for (auto &mascot : m_mascots) {
        bool inspectorWasVisible = mascot->inspectorVisible();
        auto newMascot = new ShijimaWidget(*mascot, windowedMode,
            mascotParent());
        newMascot->setEnv(env);
        delete mascot;
        mascot = newMascot;
        m_mascotsById[mascot->mascotId()] = mascot;
        mascot->mascot().reset_position();
        mascot->show();
        if (inspectorWasVisible) {
            mascot->showInspector();
        }
    }
}

ShijimaManager::ShijimaManager(QWidget *parent):
    PlatformWidget(parent, PlatformWidget::ShowOnAllDesktops),
    m_sandboxWidget(nullptr),
    m_settings("pixelomer", "Shijima-Qt"),
    m_idCounter(0), m_httpApi(this),
    m_hasTickCallbacks(false)
{
    for (auto screen : QGuiApplication::screens()) {
        screenAdded(screen);
    }
    screenAdded(nullptr);

    connect(qApp, &QGuiApplication::screenAdded,
        this, &ShijimaManager::screenAdded);
    connect(qApp, &QGuiApplication::screenRemoved,
        this, &ShijimaManager::screenRemoved);

    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString mascotsPath = QDir::cleanPath(dataPath + QDir::separator() + "mascots");
    QDir mascotsDir(mascotsPath);
    if (!mascotsDir.exists()) {
        mascotsDir.mkpath(mascotsPath);
    }

    // 自动平滑迁移旧版 Shijima-Qt 自定义桌宠皮肤数据
    QString oldDataPath = dataPath;
    oldDataPath.replace("guyi-bot", "Shijima-Qt");
    QString oldMascotsPath = QDir::cleanPath(oldDataPath + QDir::separator() + "mascots");
    QDir oldMascotsDir(oldMascotsPath);
    if (oldMascotsDir.exists() && oldMascotsPath != mascotsPath) {
        for (const auto &entry : oldMascotsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString destEntry = mascotsPath + QDir::separator() + entry.fileName();
            if (!QDir(destEntry).exists() && !QFile::exists(destEntry)) {
                QDir().mkpath(destEntry);
                QDir sourceSubDir(entry.absoluteFilePath());
                for (const auto &f : sourceSubDir.entryInfoList(QDir::Files)) {
                    QFile::copy(f.absoluteFilePath(), destEntry + QDir::separator() + f.fileName());
                }
            }
        }
    }

    if (QFile readme { mascotsDir.absoluteFilePath("README.txt") };
        readme.open(QFile::WriteOnly | QFile::NewOnly | QFile::Text))
    {
        readme.write(""
"Manually importing shimeji by copying its contents into this folder may\n"
"cause problems. You should use the import dialog in guyi-bot unless you\n"
"have a good reason not to.\n"
        );
        readme.close();
    }
    m_mascotsPath = mascotsPath;
    std::cout << "Mascots path: " << m_mascotsPath.toStdString() << std::endl;
    
    loadDefaultMascot();
    loadAllMascots();
    setAcceptDrops(true);

    m_mascotTimer = startTimer(40 / SHIJIMAQT_SUBTICK_COUNT);
    if (m_windowObserver.tickFrequency() > 0) {
        m_windowObserverTimer = startTimer(m_windowObserver.tickFrequency());
    }
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint | Qt::MaximizeUsingFullscreenGeometryHint |
        Qt::WindowMinimizeButtonHint) & ~Qt::WindowMaximizeButtonHint);
    setManagerVisible(false);

    connect(&m_listWidget, &QListWidget::itemDoubleClicked,
        this, &ShijimaManager::itemDoubleClicked);
    m_listWidget.setIconSize({ 64, 64 });
    m_listWidget.installEventFilter(this);
    m_listWidget.setSelectionMode(QListWidget::ExtendedSelection);
    m_listWidget.setContextMenuPolicy(Qt::CustomContextMenu);
    connect(&m_listWidget, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto item = m_listWidget.itemAt(pos);
        if (!item) return;
        QString rawName = item->data(Qt::UserRole).toString();
        if (rawName.isEmpty()) rawName = item->text();

        QMenu menu(this);
        auto spawnAct = menu.addAction("➕ 生成该桌宠");
        auto defAct = menu.addAction("⭐ 设为默认启动桌宠");
        menu.addSeparator();
        auto delAct = menu.addAction("🗑️ 删除桌宠");

        connect(spawnAct, &QAction::triggered, [this, rawName]() {
            spawn(rawName.toStdString());
        });
        connect(defAct, &QAction::triggered, [this, rawName]() {
            setDefaultMascot(rawName);
        });
        connect(delAct, &QAction::triggered, this, &ShijimaManager::deleteAction);

        menu.exec(m_listWidget.mapToGlobal(pos));
    });

    setCentralWidget(&m_listWidget);
    buildToolbar();

    m_httpApi.start("127.0.0.1", 32456);
    updateGlobalHotkeys();
}

void ShijimaManager::itemDoubleClicked(QListWidgetItem *qItem) {
    if (!qItem) return;
    QString rawName = qItem->data(Qt::UserRole).toString();
    if (rawName.isEmpty()) rawName = qItem->text();
    if (rawName.endsWith(" ⭐(默认)")) {
        rawName = rawName.left(rawName.length() - 8);
    }
    rawName = rawName.trimmed();
    if (!m_loadedMascots.contains(rawName)) {
        std::cerr << "[ShijimaManager] 未找到指定桌宠: " << rawName.toStdString() << std::endl;
        return;
    }
    spawn(rawName.toStdString());
}

void ShijimaManager::closeEvent(QCloseEvent *event) {
    #if !defined(__APPLE__)
    if (!m_allowClose) {
        event->ignore();
        #if defined(_WIN32)
        if (m_mascots.size() == 0) {
            askClose();
        }
        else {
            setManagerVisible(false);
        }
        #else
        askClose();
        #endif
        return;
    }
    event->accept();
    #else
    event->ignore();
    setManagerVisible(false);
    #endif
}

void ShijimaManager::timerEvent(QTimerEvent *event) {
    int timerId = event->timerId();
    if (timerId == m_mascotTimer) {
        tick();
    }
    else if (timerId == m_windowObserverTimer) {
        m_windowObserver.tick();
    }
}

void ShijimaManager::updateEnvironment(QScreen *screen) {
    if (!m_env.contains(screen)) {
        return;
    }
    auto &env = m_env[screen];
    QRect geometry, available;
    QPoint cursor;
    if (screen == nullptr) {
        if (m_sandboxWidget != nullptr) {
            geometry = m_sandboxWidget->geometry();
            cursor = m_sandboxWidget->cursor().pos() - geometry.topLeft();
            geometry.setCoords(0, 0, geometry.width(), geometry.height());
            available = geometry;
        }
        else {
            std::cerr << "warning: sandboxWidget is not initialized" << std::endl;
        }
    }
    else {
        cursor = QCursor::pos();
        geometry = screen->geometry();
        available = screen->availableGeometry();
    }
    int taskbarHeight = available.bottom() - geometry.bottom();
    int statusBarHeight = geometry.top() - available.top();
    if (taskbarHeight < 0) {
        taskbarHeight = 0;
    }
    if (statusBarHeight < 0) {
        statusBarHeight = 0;
    }
    env->screen = { (double)geometry.top() + statusBarHeight,
        (double)geometry.right(),
        (double)geometry.bottom(),
        (double)geometry.left() };
    env->floor = { (double)geometry.bottom() - taskbarHeight,
        (double)geometry.left(), (double)geometry.right() };
    env->work_area = { (double)geometry.top(),
        (double)geometry.right(),
        (double)geometry.bottom() - taskbarHeight,
        (double)geometry.left() };
    env->ceiling = { (double)geometry.top(), (double)geometry.left(),
        (double)geometry.right() };
    if (!windowedMode() && m_currentWindow.available &&
        m_currentWindow.width > 80 && m_currentWindow.height > 80)
    {
        env->active_ie = { m_currentWindow.y,
            m_currentWindow.x + m_currentWindow.width,
            m_currentWindow.y + m_currentWindow.height,
            m_currentWindow.x };
        if (m_previousWindow.available &&
            m_previousWindow.uid == m_currentWindow.uid)
        {
            env->active_ie.dy = m_currentWindow.y - m_previousWindow.y;
            if (env->active_ie.dy == 0) {
                env->active_ie.dy = m_currentWindow.height - m_previousWindow.height;
            }
            env->active_ie.dx = m_currentWindow.x - m_previousWindow.x;
            if (env->active_ie.dx == 0) {
                env->active_ie.dx = m_currentWindow.width - m_previousWindow.width;
            }
        }
    }
    else {
        env->active_ie = { -50, -50, -50, -50 };
    }
    int x = cursor.x(), y = cursor.y();
    if (m_throwImpulseTicks > 0) {
        env->cursor = { (double)x, (double)y, m_throwImpulseDx, m_throwImpulseDy };
        m_throwImpulseTicks--;
    } else {
        env->cursor = { (double)x, (double)y, x - env->cursor.x, y - env->cursor.y };
    }
    env->subtick_count = SHIJIMAQT_SUBTICK_COUNT;
    m_previousWindow = m_currentWindow;

    env->set_scale(1.0 / std::sqrt(m_userScale));
}

void ShijimaManager::setThrowImpulse(double dx, double dy) {
    m_throwImpulseDx = dx;
    m_throwImpulseDy = dy;
    m_throwImpulseTicks = 6;
}

void ShijimaManager::updateEnvironment() {
    m_currentWindow = m_windowObserver.getActiveWindow();
    if (windowedMode()) {
        updateEnvironment(nullptr);
    }
    else {
        for (auto screen : QGuiApplication::screens()) {
            updateEnvironment(screen);
        }
    }
}

void ShijimaManager::askClose() {
    setManagerVisible(true);
    QMessageBox msgBox { this };
    msgBox.setWindowTitle("关闭 guyi-bot");
    msgBox.setIcon(QMessageBox::Icon::Question);
    msgBox.setStandardButtons(QMessageBox::StandardButton::Yes |
        QMessageBox::StandardButton::No);
    msgBox.setText("确定要退出 guyi-bot 吗？");
    int ret = msgBox.exec();
    if (ret == QMessageBox::Button::Yes) {
        #if defined(__APPLE__)
        QCoreApplication::quit();
        #else
        m_allowClose = true;
        close();
        #endif
    }
}

void ShijimaManager::setManagerVisible(bool visible) {
    #if !defined(__APPLE__)
    auto screen = QGuiApplication::primaryScreen();
    auto geometry = screen->geometry();
    if (!m_wasVisible && visible) {
        if (window() != nullptr) {
            window()->activateWindow();
        }
        setMinimumSize(480, 320);
        setMaximumSize(999999, 999999);
        move(geometry.width() / 2 - 240, geometry.height() / 2 - 160);
        m_wasVisible = true;
    }
    else if (m_wasVisible && !visible) {
        setFixedSize(1, 1);
        move(geometry.width() * 10, geometry.height() * 10);
        clearFocus();
        if (window() != nullptr) {
            window()->activateWindow();
        }
        m_wasVisible = false;
    }
    #else
    if (visible) {
        show();
        m_wasVisible = true;
    }
    else {
        hide();
        m_wasVisible = false;
    }
    #endif
}

bool ShijimaManager::windowedMode() {
    return m_sandboxWidget != nullptr;
}

QWidget *ShijimaManager::mascotParent() {
    if (windowedMode()) {
        return m_sandboxWidget;
    }
    else {
        return this;
    }
}

void ShijimaManager::tick() {
    if (m_hasTickCallbacks) {
        auto lock = acquireLock();
        for (auto &callback : m_tickCallbacks) {
            callback(this);
        }
        m_tickCallbacks.clear();
        m_hasTickCallbacks = false;
        m_tickCallbackCompletion.notify_all();
    }

    if (m_sandboxWidget != nullptr && !m_sandboxWidget->isVisible()) {
        setWindowedMode(false);
        #if !defined(__APPLE__)
        if (m_mascots.size() == 0) {
            setManagerVisible(true);
        }
        #endif
    }

    #if !defined(__APPLE__)
    if (isMinimized()) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
        setManagerVisible(!m_wasVisible);
    }
    else if (isMaximized()) {
        setManagerVisible(true);
    }
    #endif

    if (m_mascots.size() == 0) {
        #if !defined(__APPLE__)
        if (!windowedMode() && (isMinimized() || !m_wasVisible)) {
            setWindowState(windowState() & ~Qt::WindowMinimized);
            setManagerVisible(true);
        }
        #endif
        return;
    }

    updateEnvironment();

    for (auto iter = m_mascots.end(); iter != m_mascots.begin(); ) {
        --iter;
        ShijimaWidget *shimeji = *iter;
        if (shimeji->isMarkedForDeletion()) {
            int mascotId = shimeji->mascotId();
            shimeji->deleteLater();
            auto erasePos = iter;
            ++iter;
            m_mascots.erase(erasePos);
            m_mascotsById.erase(mascotId);
            continue;
        }
        shimeji->tick();
        auto &mascot = shimeji->mascot();
        auto &breedRequest = mascot.state->breed_request;
        if (mascot.state->dragging && !windowedMode()) {
            auto oldScreen = m_reverseEnv[mascot.state->env.get()];
            auto newScreen = QGuiApplication::screenAt(QPoint {
                (int)mascot.state->anchor.x, (int)mascot.state->anchor.y });
            if (newScreen != nullptr && oldScreen != newScreen) {
                mascot.state->env = m_env[newScreen];
            }
        }
        if (breedRequest.available) {
            if (breedRequest.name == "") {
                breedRequest.name = shimeji->mascotName().toStdString();
            }
            // only consider the last path component
            breedRequest.name = breedRequest.name.substr(breedRequest.name.rfind('\\')+1);
            breedRequest.name = breedRequest.name.substr(breedRequest.name.rfind('/')+1);
            std::optional<shijima::mascot::factory::product> product;
            try {
                product = m_factory.spawn(breedRequest);
            }
            catch (std::exception &ex) {
                std::cerr << "couldn't fulfill breed request for "
                    << breedRequest.name << std::endl;
                std::cerr << ex.what() << std::endl;
            }
            if (product.has_value()) {
                ShijimaWidget *child = new ShijimaWidget(
                    m_loadedMascots[QString::fromStdString(breedRequest.name)],
                    std::move(product->manager), m_idCounter++,
                    windowedMode(), mascotParent());
                child->setEnv(shimeji->env());
                child->show();
                m_mascots.push_back(child);
                m_mascotsById[child->mascotId()] = child;
            }
            breedRequest.available = false;
        }
    }
    
    for (auto &env : m_env) {
        env->reset_scale();
    }

    if (m_mascots.size() == 0 && !windowedMode()) {
        // All mascots self-destructed, show manager
        setManagerVisible(true);
    }
}

ShijimaWidget *ShijimaManager::hitTest(QPoint const& screenPos) {
    for (auto mascot : m_mascots) {
        QPoint localPos = { screenPos.x() - mascot->x(),
            screenPos.y() - mascot->y() };
        if (mascot->pointInside(localPos)) {
            return mascot;
        }
    }
    return nullptr;
}

QScreen *ShijimaManager::mascotScreen() {
    QScreen *screen;
    if (windowedMode()) {
        screen = nullptr;
    }
    else {
        screen = this->screen();
        if (screen == nullptr) {
            screen = qApp->primaryScreen();
        }
    }
    return screen;
}

ShijimaWidget *ShijimaManager::spawn(std::string const& name) {
    QString qName = QString::fromStdString(name);
    if (!m_loadedMascots.contains(qName)) {
        std::cerr << "[ShijimaManager] 无法生成未加载的桌宠: " << name << std::endl;
        return nullptr;
    }
    QScreen *screen = mascotScreen();
    updateEnvironment(screen);
    auto &env = m_env[screen];
    try {
        auto product = m_factory.spawn(name, {});
        if (!product.manager || !product.manager->state) {
            std::cerr << "[ShijimaManager] 生成桌宠管理器失败: " << name << std::endl;
            return nullptr;
        }
        product.manager->state->env = env;
        product.manager->reset_position();
        ShijimaWidget *shimeji = new ShijimaWidget(
            m_loadedMascots[qName],
            std::move(product.manager), m_idCounter++,
            windowedMode(), mascotParent());
        shimeji->show();
        m_mascots.push_back(shimeji);
        m_mascotsById[shimeji->mascotId()] = shimeji;
        env->reset_scale();
        return shimeji;
    } catch (const std::exception &e) {
        std::cerr << "[ShijimaManager] 生成桌宠异常: " << e.what() << std::endl;
        return nullptr;
    }
}

bool ShijimaManager::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        auto key = keyEvent->key();
        if (key == Qt::Key::Key_Return || key == Qt::Key::Key_Enter) {
            for (auto item : m_listWidget.selectedItems()) {
                itemDoubleClicked(item);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void ShijimaManager::spawnClicked() {
    auto &allTemplates = m_factory.get_all_templates();
    int target = QRandomGenerator::global()->bounded((int)allTemplates.size());
    int i = 0;
    for (auto &pair : allTemplates) {
        if (i++ != target) continue;
        std::cout << "Spawning: " << pair.first << std::endl;
        spawn(pair.first);
        break;
    }
}