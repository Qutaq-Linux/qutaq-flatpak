#pragma once

#include <QStringList>
#include <QTranslator>

class QComboBox;
class QLabel;

class Localization
{
public:
    Localization();
    QComboBox *createLanguageSelector(QLabel *status) const;

private:
    QStringList catalogLocales;
    QTranslator qtTranslator;
    QTranslator appTranslator;
};
