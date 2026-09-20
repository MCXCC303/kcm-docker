<!-- SPDX-FileCopyrightText: 2026 kcm-docker developers -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Translations

Standard KDE layout:

```
po/
├── kcm_docker.pot         # template (extracted by xgettext; for reference/merging)
└── <lang>/
    └── kcm_docker.po      # domain = kcm_docker (= KCM plugin id, same as kTranslationDomain)
```

At build time `ki18n_install(po)` compiles every `po/<lang>/kcm_docker.po` into
`${KDE_INSTALL_LOCALEDIR}/<lang>/LC_MESSAGES/kcm_docker.mo` and installs it.
After `source build/prefix.sh` that directory is inside the prefix, so
KLocalizedString finds the translations.

## Re-extracting the template

```bash
find src -name '*.cpp' -o -name '*.h' -o -name '*.qml' | xargs xgettext \
  --language=C++ --from-code=UTF-8 \
  --keyword=i18n --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
  --package-name=kontainer -o po/kcm_docker.pot
```

## Adding a language

```bash
mkdir -p po/<lang>
msginit -i po/kcm_docker.pot -o po/<lang>/kcm_docker.po -l <lang>
```

## Verifying

```bash
LANGUAGE=zh_CN kcmshell6 kcm_docker     # source build/prefix.sh first
```

Automated checks (`tst_i18n_consistency`; i18n is only healthy when all four pass):

1. translation domain == plugin id == metadata `TranslationDomain`
2. `po/zh_CN` has no untranslated entries (`msgfmt --check` checks format, not gaps)
3. `po/kcm_docker.pot` matches the sources — the test reruns the `xgettext` command above and
   compares sets, so **a UI string change requires re-extracting** or the test fails (mixed-language UI)
4. the `.mo` really loads: looked up at `<XDG_DATA_DIRS>/locale/<lang>/LC_MESSAGES/kcm_docker.mo`
   and a few translations asserted (wrong domain, language or dir silently reverts to English)

Screenshot review of the Chinese layout (Chinese is longer; wrapping and button widths only show there):

```bash
KCM_DOCKER_RENDER_LANG=zh_CN tests/tools/render_ui.sh daemon-config 1200 950 dark /tmp/zh.png
```

> `xgettext` warns about URLs inside strings that need translation (e.g. example
> addresses). Split them out as arguments: `i18n("… example %1", QStringLiteral("https://…"))` —
> URLs need no translation, and leaving them in the msgid only invites translators to edit them.
