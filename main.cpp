#include "localization.h"

#include <QtWidgets>
#include <QProcess>
#include <algorithm>

namespace {
struct Result {
    bool ok;
    QString output;
    QString error;
};

Result flatpak(const QStringList &arguments)
{
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start("flatpak", arguments);
    if (!process.waitForStarted())
        return {false, {}, process.errorString()};
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished();
        return {false, {}, QCoreApplication::translate("Flatpak",
            "Flatpak did not respond within 8 seconds. Check the current state before retrying.")};
    }
    const auto error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return {ok, QString::fromUtf8(process.readAllStandardOutput()), error.isEmpty()
        ? QCoreApplication::translate("Flatpak", "Flatpak exited with code %1.").arg(process.exitCode()) : error};
}

QStringList metadataList(const QString &value)
{
    QStringList entries;
    QString entry;
    bool escaped = false;
    for (const QChar character : value) {
        if (escaped) {
            switch (character.unicode()) {
            case 's': entry += ' '; break;
            case 'n': entry += '\n'; break;
            case 't': entry += '\t'; break;
            case 'r': entry += '\r'; break;
            default: entry += character; break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == ';') {
            if (!entry.isEmpty())
                entries.append(entry);
            entry.clear();
        } else {
            entry += character;
        }
    }
    if (!entry.isEmpty())
        entries.append(entry);
    return entries;
}

struct Permission {
    const char *label;
    QString group;
    QString value;
    QString option;
};

const QList<Permission> permissions = {
    { QT_TRANSLATE_NOOP("Window", "Internet and local network"), "shared", "network", "share" },
    { QT_TRANSLATE_NOOP("Window", "X11 shared memory"), "shared", "ipc", "share" },
    { QT_TRANSLATE_NOOP("Window", "PulseAudio sound server"), "sockets", "pulseaudio", "socket" },
    { QT_TRANSLATE_NOOP("Window", "Wayland display server"), "sockets", "wayland", "socket" },
    { QT_TRANSLATE_NOOP("Window", "X11 display server"), "sockets", "x11", "socket" },
    { QT_TRANSLATE_NOOP("Window", "Fallback X11 access"), "sockets", "fallback-x11", "socket" },
    { QT_TRANSLATE_NOOP("Window", "SSH agent"), "sockets", "ssh-auth", "socket" },
    { QT_TRANSLATE_NOOP("Window", "Smart cards"), "sockets", "pcsc", "socket" },
    { QT_TRANSLATE_NOOP("Window", "CUPS printing system"), "sockets", "cups", "socket" },
    { QT_TRANSLATE_NOOP("Window", "Bluetooth"), "features", "bluetooth", "allow" },
    { QT_TRANSLATE_NOOP("Window", "Graphics hardware (DRI)"), "devices", "dri", "device" },
    { QT_TRANSLATE_NOOP("Window", "All devices"), "devices", "all", "device" },
};

const QList<QPair<QString, const char *>> locations = {
    {"home", QT_TRANSLATE_NOOP("Window", "Home folder")},
    {"host", QT_TRANSLATE_NOOP("Window", "Host files")},
    {"host-os", QT_TRANSLATE_NOOP("Window", "System libraries and executables")},
    {"host-etc", QT_TRANSLATE_NOOP("Window", "System configuration")},
    {"xdg-desktop", QT_TRANSLATE_NOOP("Window", "Desktop")},
    {"xdg-documents", QT_TRANSLATE_NOOP("Window", "Documents")},
    {"xdg-download", QT_TRANSLATE_NOOP("Window", "Downloads")},
    {"xdg-pictures", QT_TRANSLATE_NOOP("Window", "Pictures")},
    {"xdg-music", QT_TRANSLATE_NOOP("Window", "Music")},
    {"xdg-videos", QT_TRANSLATE_NOOP("Window", "Videos")},
};

const QList<const char *> modes = {
    QT_TRANSLATE_NOOP("Window", "Explicitly denied"),
    QT_TRANSLATE_NOOP("Window", "Read-only"),
    QT_TRANSLATE_NOOP("Window", "Read and write"),
    QT_TRANSLATE_NOOP("Window", "Create and write"),
};
const QStringList suffixes = {"", ":ro", "", ":create"};

QString revokeOption(const QString &option)
{
    if (option == "share") return "unshare";
    if (option == "allow") return "disallow";
    return "no" + option;
}

struct Application {
    QString name, id, version, installation, ref;
};

struct State {
    QSet<QString> flags;
    QMap<QString, int> filesystem;
};
} // namespace

class Window final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(Window)

public:
    explicit Window(const Localization &localization)
    {
        setWindowTitle(QApplication::applicationDisplayName());
        resize(1110, 750);
        setMinimumSize(850, 520);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 18, 22, 16);
        root->setSpacing(12);

        auto *split = new QSplitter;
        split->setChildrenCollapsible(false);
        auto *sidebar = new QWidget;
        auto *side = new QVBoxLayout(sidebar);
        side->setContentsMargins(0, 0, 10, 0);
        search = new QLineEdit;
        search->setPlaceholderText(tr("Search by name or ID"));
        search->setClearButtonEnabled(true);
        side->addWidget(search);
        apps = new QListWidget;
        apps->setAccessibleName(tr("Installed applications"));
        apps->setIconSize(QSize(28, 28));
        side->addWidget(apps, 1);
        split->addWidget(sidebar);

        auto *right = new QWidget;
        auto *panel = new QVBoxLayout(right);
        panel->setContentsMargins(8, 0, 0, 0);
        title = new QLabel;
        title->setTextFormat(Qt::PlainText);
        title->setObjectName("title");
        detail = new QLabel;
        detail->setTextFormat(Qt::PlainText);
        detail->setWordWrap(true);
        detail->setObjectName("muted");
        appIcon = new QLabel;
        appIcon->setAlignment(Qt::AlignCenter);
        appIcon->setFixedSize(48, 48);
        appIcon->hide();
        auto *identity = new QHBoxLayout;
        auto *identityText = new QVBoxLayout;
        identityText->addWidget(title);
        identityText->addWidget(detail);
        identity->addWidget(appIcon);
        identity->addLayout(identityText, 1);
        panel->addLayout(identity);

        scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto *content = new QWidget;
        auto *sections = new QVBoxLayout(content);
        sections->setContentsMargins(0, 0, 6, 6);
        auto *basic = new QGroupBox(tr("Basic permissions"));
        auto *grid = new QGridLayout(basic);
        for (qsizetype i = 0; i < permissions.size(); ++i) {
            auto *checkbox = new QCheckBox(tr(permissions[i].label));
            checks.append(checkbox);
            grid->addWidget(checkbox, i / 2, i % 2);
            connect(checkbox, &QCheckBox::toggled, this, [this] { updateButtons(); });
        }
        sections->addWidget(basic);
        auto *files = new QGroupBox(tr("Filesystem access"));
        auto *fileLayout = new QVBoxLayout(files);
        fileLayout->addWidget(new QLabel(tr("Access may be inherited from a parent folder.")));
        table = new QTableWidget(0, 2);
        table->setHorizontalHeaderLabels({tr("Location"), tr("Access")});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setMinimumHeight(310);
        fileLayout->addWidget(table);
        auto *add = new QPushButton(tr("Add path"));
        fileLayout->addWidget(add, 0, Qt::AlignRight);
        sections->addWidget(files);
        sections->addStretch();
        scroll->setWidget(content);
        panel->addWidget(scroll, 1);
        split->addWidget(right);
        split->setSizes({290, 760});
        split->setStretchFactor(1, 1);
        root->addWidget(split, 1);

        auto *actions = new QHBoxLayout;
        defaults = new QPushButton(tr("Reset overrides"));
        undo = new QPushButton(tr("Discard changes"));
        apply = new QPushButton(tr("Apply"));
        apply->setObjectName("apply");
        status = new QLabel;
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName("muted");
        actions->addWidget(defaults);
        actions->addWidget(undo);
        actions->addWidget(status, 1);
        actions->addWidget(localization.createLanguageSelector(status));
        actions->addWidget(apply);
        root->addLayout(actions);

        connect(apps, &QListWidget::currentRowChanged, this, [this](int row) { select(row); });
        connect(search, &QLineEdit::textChanged, this, [this](const QString &filter) {
            for (int i = 0; i < apps->count(); ++i)
                apps->item(i)->setHidden(!applications[i].name.contains(filter, Qt::CaseInsensitive)
                                      && !applications[i].id.contains(filter, Qt::CaseInsensitive));
        });
        connect(add, &QPushButton::clicked, this, [this] { addPath(); });
        connect(undo, &QPushButton::clicked, this, [this] { render(); status->clear(); });
        connect(apply, &QPushButton::clicked, this, [this] { save(); });
        connect(defaults, &QPushButton::clicked, this, [this] { reset(); });
        reload();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (discardChanges()) event->accept();
        else event->ignore();
    }

private:
    QList<Application> applications;
    State original;
    QList<QCheckBox *> checks;
    QMap<QString, QComboBox *> paths;
    QListWidget *apps;
    QLineEdit *search;
    QLabel *title, *detail, *status, *appIcon;
    QScrollArea *scroll;
    QTableWidget *table;
    QPushButton *apply, *undo, *defaults;
    int current = -1;
    bool loaded = false;
    bool rendering = false;

    bool discardChanges()
    {
        return changes().isEmpty() || QMessageBox::question(this, tr("Unsaved changes"),
            tr("Discard changes for the selected application?"), QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Discard;
    }

    void reload()
    {
        const QSignalBlocker block(apps);
        apps->clear();
        applications.clear();
        current = -1;
        loaded = false;
        search->clear();
        status->clear();
        detail->clear();
        title->setText(tr("Select an application"));
        appIcon->hide();
        scroll->hide();
        updateButtons();
        const QStringList listArgs{"list", "--app", "--columns=name,application,version,installation,ref"};
        auto result = flatpak(listArgs);
        QString listNotice;
        if (!result.ok && result.error.contains("repository")) {
            auto personal = flatpak(listArgs + QStringList{"--user"});
            if (personal.ok) {
                result = std::move(personal);
                listNotice = tr("The system installation is unavailable. Showing user applications.");
            }
        }
        if (!result.ok) {
            title->setText(tr("Could not read the application list"));
            detail->setText(result.error);
            return;
        }
        for (const auto &line : result.output.split('\n', Qt::SkipEmptyParts)) {
            const auto fields = line.split('\t');
            if (fields.size() != 5 || fields[4].count('/') < 2)
                continue;
            applications.append({fields[0].isEmpty() ? fields[1] : fields[0],
                                 fields[1], fields[2], fields[3], fields[4]});
        }
        std::stable_sort(applications.begin(), applications.end(), [](const auto &a, const auto &b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
        for (const auto &app : applications) {
            auto icon = QIcon::fromTheme(app.id, style()->standardIcon(QStyle::SP_ComputerIcon));
            const auto duplicateCount = std::count_if(applications.cbegin(), applications.cend(),
                [&app](const auto &other) { return other.id == app.id; });
            auto *item = new QListWidgetItem(icon, duplicateCount > 1
                ? app.name + " · " + app.installation + " · " + app.ref.section('/', -1) : app.name, apps);
            item->setToolTip(app.id);
        }
        if (applications.isEmpty()) {
            title->setText(tr("No Flatpak applications found"));
            status->setText(listNotice);
            return;
        }
        apps->setCurrentRow(0);
        select(0);
        if (loaded)
            status->setText(listNotice);
    }

    void select(int row)
    {
        if (row < 0 || row == current)
            return;
        if (!discardChanges()) {
            const QSignalBlocker block(apps);
            apps->setCurrentRow(current);
            return;
        }
        current = row;
        status->clear();
        loadPermissions();
    }

    void loadPermissions()
    {
        const auto &app = applications[current];
        title->setText(app.name);
        appIcon->setPixmap(apps->item(current)->icon().pixmap(42, 42));
        appIcon->show();
        detail->setText(QStringList{app.id, app.version, app.installation, app.ref.section('/', -1)}
                        .filter(QRegularExpression(".+")).join("  ·  "));
        const auto scope = app.installation == "user" ? "--user"
            : app.installation == "system" ? "--system" : "--installation=" + app.installation.mid(8, app.installation.size() - 9);
        const auto result = flatpak({"info", scope, "--show-permissions", app.ref});
        loaded = result.ok;
        if (!loaded) {
            scroll->hide();
            status->setText(result.error);
            updateButtons();
            return;
        }
        original = {};
        bool context = false;
        for (const auto &line : result.output.split('\n')) {
            if (line.startsWith('['))
                context = line == "[Context]";
            if (!context || !line.contains('='))
                continue;
            const auto group = line.section('=', 0, 0);
            for (const auto &value : metadataList(line.section('=', 1))) {
                if (group == "filesystems") {
                    if (value.startsWith('!'))
                        original.filesystem.insert(value.mid(1), 0);
                    else if (value.endsWith(":ro"))
                        original.filesystem.insert(value.chopped(3), 1);
                    else if (value.endsWith(":create"))
                        original.filesystem.insert(value.chopped(7), 3);
                    else
                        original.filesystem.insert(value.endsWith(":rw") ? value.chopped(3) : value, 2);
                } else if (!value.startsWith('!')) {
                    original.flags.insert(group + '/' + value);
                }
            }
        }
        render();
        scroll->show();
        scroll->verticalScrollBar()->setValue(0);
    }

    void render()
    {
        rendering = true;
        for (qsizetype i = 0; i < permissions.size(); ++i) {
            const auto &permission = permissions[i];
            checks[i]->setChecked(original.flags.contains(permission.group + '/' + permission.value));
        }
        paths.clear();
        table->setRowCount(0);
        for (const auto &[path, label] : locations)
            appendPath(path, tr(label));
        for (auto it = original.filesystem.cbegin(); it != original.filesystem.cend(); ++it) {
            if (!paths.contains(it.key()) && it.key() != "host:reset")
                appendPath(it.key(), it.key());
        }
        rendering = false;
        updateButtons();
    }

    void appendPath(const QString &path, const QString &label)
    {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setRowHeight(row, 39);
        auto *cell = new QTableWidgetItem(label);
        cell->setToolTip(path);
        table->setItem(row, 0, cell);
        auto *combo = new QComboBox;
        combo->setAccessibleName(label);
        if (!original.filesystem.contains(path))
            combo->addItem(tr("No explicit rule"), -1);
        for (int mode = 0; mode < modes.size(); ++mode)
            combo->addItem(tr(modes[mode]), mode);
        combo->setCurrentIndex(combo->findData(original.filesystem.value(path, -1)));
        table->setCellWidget(row, 1, combo);
        paths.insert(path, combo);
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { updateButtons(); });
    }

    void addPath()
    {
        bool ok;
        const auto path = QInputDialog::getText(this, tr("Add path"),
            tr("Absolute path, ~/folder or xdg-documents/folder"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok)
            return;
        static const QRegularExpression pattern(R"(^(?:/[^:\r\n]*|~/[^:\r\n]+|xdg-(?:desktop|documents|download|music|pictures|public-share|templates|videos|config|cache|data|run)(?:/[^:\r\n]+)?)$)");
        if (!pattern.match(path).hasMatch()) {
            QMessageBox::warning(this, tr("Invalid path"), tr("Enter a path in one of the listed formats."));
            return;
        }
        if (!paths.contains(path))
            appendPath(path, path);
        auto *combo = paths.value(path);
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->cellWidget(row, 1) == combo) {
                table->scrollToItem(table->item(row, 0));
                break;
            }
        }
        combo->setFocus();
        combo->showPopup();
    }

    QStringList changes() const
    {
        if (!loaded)
            return {};
        QStringList options;
        for (qsizetype i = 0; i < permissions.size(); ++i) {
            const auto &permission = permissions[i];
            const auto enabled = checks[i]->isChecked();
            if (enabled != original.flags.contains(permission.group + '/' + permission.value))
                options << "--" + (enabled ? permission.option : revokeOption(permission.option)) + '=' + permission.value;
        }
        for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
            const int mode = it.value()->currentData().toInt();
            if (mode == original.filesystem.value(it.key(), -1))
                continue;
            options << (mode == 0 ? "--nofilesystem=" + it.key()
                                 : "--filesystem=" + it.key() + suffixes[mode]);
        }
        return options;
    }

    void updateButtons()
    {
        if (rendering)
            return;
        const bool dirty = !changes().isEmpty();
        apply->setEnabled(dirty);
        undo->setEnabled(dirty);
        defaults->setEnabled(loaded);
    }

    void save()
    {
        const auto options = changes();
        if (options.isEmpty())
            return;
        const auto result = flatpak(QStringList{"override", "--user"} + options + QStringList{applications[current].id});
        if (!result.ok) {
            QMessageBox::warning(this, tr("Could not apply permissions"), result.error);
            return;
        }
        status->setText(tr("Permissions saved. Restart the selected application."));
        loadPermissions();
    }

    void reset()
    {
        if (QMessageBox::question(this, tr("Reset permissions"),
            tr("Remove all user overrides for this ID? This includes settings made with other tools."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        const auto result = flatpak({"override", "--user", "--reset", applications[current].id});
        if (!result.ok) {
            QMessageBox::warning(this, tr("Could not reset overrides"), result.error);
            return;
        }
        status->setText(tr("Overrides removed. Restart the selected application."));
        loadPermissions();
    }
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const auto applicationName = QStringLiteral("qutaq-flatpak");
    const auto fallbackIcon = QIcon::fromTheme("security-high", QIcon::fromTheme("security-high-symbolic"));
    application.setWindowIcon(QIcon::fromTheme("preferences-security", fallbackIcon));
    application.setDesktopFileName(applicationName);
    application.setOrganizationName("Qutaq");
    application.setApplicationName(applicationName);
    application.setApplicationDisplayName(applicationName);

    const Localization localization;
    Window window(localization);
    window.show();
    return application.exec();
}
