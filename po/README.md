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

自动检查（`tst_i18n_consistency`，四条一起才说明 i18n 是好的）：

1. 翻译域 == 插件 id == 元数据 `TranslationDomain`
2. `po/zh_CN` 没有未翻译条目（`msgfmt --check` 只查格式，不查漏译）
3. `po/kcm_docker.pot` 与源码同步——测试会现场跑一遍上面那条 `xgettext` 命令并比对集合，
   所以**改了界面文案就要重新提取模板**，否则测试直接失败（漏掉的那条在界面上表现为中英混排）
4. 运行时真的能加载 `.mo`：按 `<XDG_DATA_DIRS>/locale/<lang>/LC_MESSAGES/kcm_docker.mo`
   找一次并断言几条译文（域、语言、安装目录任一环错都会静默退回英文）

截图复核中文排版（中文比英文长，横幅折行与按钮宽度只有看截图才知道）：

```bash
KCM_DOCKER_RENDER_LANG=zh_CN tests/tools/render_ui.sh daemon-config 1200 950 dark /tmp/zh.png
```

> `xgettext` 会对"待译字符串里带 URL"给出警告（例如示例地址）。
> 处理方式是把它拆成参数：`i18n("… example %1", QStringLiteral("https://…"))`——
> URL 不需要翻译，混进 msgid 只会让译者去改动它。
