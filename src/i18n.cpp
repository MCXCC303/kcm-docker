/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"

#include <KLocalizedString>

namespace Kontainer
{

void setupTranslationDomain()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
    KLocalizedString::setApplicationDomain(QByteArray(kTranslationDomain));
}

} // namespace Kontainer
