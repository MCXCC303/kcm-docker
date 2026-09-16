/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    数据可视化色板（ARCH_V3 §2.4 / ARCH_V3_pre §1.4）。

    §1.4 明确把两类颜色列为**语义色之外的例外**：
      1. sparkline / 趋势图的序列色（CPU / 内存 / 网络 / 块 IO）需要彼此可区分；
      2. 存储占用堆叠条属于中性数据展示。
    因此这里使用固定的低饱和度色值，并按亮色 / 暗色主题各定义一版——
    而不是借用 positiveTextColor / negativeTextColor 这类**状态语义** token。

    本文件与 StatusPalette 互不引用：状态语义色与数据序列色必须保持独立，
    否则「绿色」会同时表示「运行中」和「网络流量」，语义就退化了。

    ## 对比度

    所有取色都对目标主题背景做过 WCAG 对比度校验（非文本图形元素要求 3:1，
    此处按更严格的 4.5:1 取色）：

    | 主题 | 背景基准 | 序列色最低对比度 |
    | --- | --- | --- |
    | 亮色 | `#eff0f1`（Breeze Light view 背景） | 5.5:1 |
    | 暗色 | `#232629`（Breeze Dark view 背景，§1.8 指定） | 6.5:1 |

    存储色阶（同色相四级明度）在两种主题下均 ≥ 4.5:1，相邻级差 1.19–1.28:1；
    由于级差较小，堆叠条**必须**在段与段之间绘制背景色分隔线，
    并提供「颜色 + 文字 + 数值」三重编码的图例，不能只靠颜色区分（§1.8）。

    ## 明暗判定

    Kirigami 6.30 的 QML API **没有**暴露 `colorScheme` / `Theme.Light` / `Theme.Dark`
    （实测：`Kirigami.Theme.colorScheme` 与 `Platform.Theme.colorScheme` 均为 undefined，
    qmltypes 中也不存在该属性）。因此这里改用不依赖枚举的方式判断：
    比较背景色与文字色的相对亮度——文字总是与背景形成对比，
    谁更亮就说明当前是哪种配色方案。该判定对亮 / 暗 / 高对比主题都成立。
*/

pragma Singleton

import QtQuick

import org.kde.kirigami as Kirigami

QtObject {
    /*!
        单通道 sRGB → 线性亮度分量（WCAG 2.x 定义）。
    */
    function linearize(channel: real): real {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    }

    /*! 颜色的相对亮度（0 = 黑，1 = 白）。 */
    function relativeLuminance(c: color): real {
        return 0.2126 * linearize(c.r) + 0.7152 * linearize(c.g) + 0.0722 * linearize(c.b);
    }

    /*! 两色的 WCAG 对比度，供测试与校验使用。 */
    function contrastRatio(a: color, b: color): real {
        const la = relativeLuminance(a);
        const lb = relativeLuminance(b);
        const lighter = Math.max(la, lb);
        const darker = Math.min(la, lb);
        return (lighter + 0.05) / (darker + 0.05);
    }

    /*! 当前是否为暗色配色方案。 */
    readonly property bool darkScheme: relativeLuminance(Kirigami.Theme.backgroundColor) < relativeLuminance(Kirigami.Theme.textColor)

    /*! 校验用的背景基准（Breeze Light / Dark 的 view 背景）。 */
    readonly property color lightReferenceBackground: "#eff0f1"
    readonly property color darkReferenceBackground: "#232629"

    /* ---------------- 趋势图序列色 ---------------- */

    readonly property color cpuSeries: darkScheme ? "#6fb3ff" : "#0b5d9e"
    readonly property color memorySeries: darkScheme ? "#f0a35e" : "#8a5300"
    readonly property color networkSeries: darkScheme ? "#6fcf97" : "#186b3a"
    readonly property color blockSeries: darkScheme ? "#b79cf0" : "#6b3fa0"

    /* ---------------- 存储占用堆叠条（同色相四级明度） ---------------- */

    readonly property color storageImages: darkScheme ? "#4098e0" : "#12456e"
    readonly property color storageContainers: darkScheme ? "#65ace6" : "#165589"
    readonly property color storageVolumes: darkScheme ? "#87beeb" : "#1a64a0"
    readonly property color storageBuildCache: darkScheme ? "#a9d0f1" : "#1d70b4"

    /*!
        段与段之间的分隔线颜色。
        存储色阶的相邻级差只有约 1.2:1，靠颜色本身不足以稳定区分，
        因此堆叠条必须绘制这条分隔线（§1.8：颜色不能是唯一区分手段）。
    */
    readonly property color storageSeparator: Kirigami.Theme.backgroundColor
}
