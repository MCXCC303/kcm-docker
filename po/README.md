<!-- SPDX-FileCopyrightText: 2026 kontainer developers -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# 翻译

采用 KDE 标准布局：

```
po/
├── kcm_docker.pot         # 模板（由 xgettext 提取，仅供参考/合并用）
└── <lang>/
    └── kcm_docker.po      # 翻译域 = kcm_docker（= KCM 插件 id，与 kTranslationDomain 一致）
```

构建时 `ki18n_install(po)` 会把每个 `po/<lang>/kcm_docker.po` 编译成
`${KDE_INSTALL_LOCALEDIR}/<lang>/LC_MESSAGES/kcm_docker.mo` 并安装。
`source build/prefix.sh` 之后 `KDE_INSTALL_LOCALEDIR` 位于前缀内，KLocalizedString
即可找到译文。

## 重新提取模板

```bash
find src -name '*.cpp' -o -name '*.h' -o -name '*.qml' | xargs xgettext \
  --language=C++ --from-code=UTF-8 \
  --keyword=i18n --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
  --package-name=kontainer -o po/kcm_docker.pot
```

## 新增语言

```bash
mkdir -p po/<lang>
msginit -i po/kcm_docker.pot -o po/<lang>/kcm_docker.po -l <lang>
```

## 验证

```bash
LANGUAGE=zh_CN kcmshell6 kcm_docker     # 需要先 source build/prefix.sh
```
