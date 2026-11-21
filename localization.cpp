#include "localization.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace {
const auto languageKey = QStringLiteral("interface/language");
const auto resourceDir = QStringLiteral(":/i18n");
const auto catalogName = QStringLiteral("qutaq-flatpak");
const auto catalogPrefix = catalogName + '_';
const auto catalogSuffix = QStringLiteral(".qm");

QString savedLanguage()
{
    const auto value = QSettings().value(languageKey);
    if (value.isValid())
        return value.toString();
    return QSettings("FlatpakAccess", "flatpak-access").value(languageKey).toString();
}
}

Localization::Localization()
{
    const auto files = QDir(resourceDir).entryList({catalogPrefix + '*' + catalogSuffix}, QDir::Files);
    for (const auto &file : files)
        catalogLocales.append(file.chopped(catalogSuffix.size()).mid(catalogPrefix.size()));

    const auto saved = savedLanguage();
    const auto fallback = QStringLiteral("en");
    const bool loaded = (catalogLocales.contains(saved)
                         && appTranslator.load(catalogPrefix + saved + catalogSuffix, resourceDir))
        || appTranslator.load(QLocale::system(), catalogName, "_", resourceDir)
        || appTranslator.load(catalogPrefix + fallback + catalogSuffix, resourceDir);

    const auto locale = QLocale(loaded ? appTranslator.language() : fallback);
    if (qtTranslator.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator);
    if (loaded)
        QCoreApplication::installTranslator(&appTranslator);
}

QComboBox *Localization::createLanguageSelector(QLabel *status) const
{
    auto *selector = new QComboBox;
    const auto label = QCoreApplication::translate("Window", "Interface language");
    selector->setAccessibleName(label);
    selector->setToolTip(label);
    selector->addItem(QCoreApplication::translate("Window", "System language"), QStringLiteral(""));

    for (const auto &code : catalogLocales) {
        auto name = QLocale(code).nativeLanguageName();
        if (name.isEmpty())
            name = code;
        else if (code.contains('_') || code.contains('-'))
            name += QStringLiteral(" (%1)").arg(code);
        selector->addItem(name, code);
    }
    selector->setCurrentIndex(qMax(0, selector->findData(savedLanguage())));

    QObject::connect(selector, &QComboBox::activated, status, [selector, status](int index) {
        QSettings settings;
        const auto previous = savedLanguage();
        const auto selection = selector->itemData(index).toString();
        if (selection == previous)
            return;
        settings.setValue(languageKey, selection);
        settings.sync();
        if (settings.status() == QSettings::NoError) {
            status->setText(QCoreApplication::translate("Window",
                "Restart qutaq-flatpak to apply the interface language."));
        } else {
            settings.setValue(languageKey, previous);
            selector->setCurrentIndex(qMax(0, selector->findData(previous)));
            status->setText(QCoreApplication::translate("Window", "Could not save the interface language."));
        }
    });
    return selector;
}
