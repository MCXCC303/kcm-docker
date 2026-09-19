/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    状态语义 → 主题颜色 / 徽标类型的**唯一**映射（ARCH_V3 §2.1）。

    输入 key 来自 C++ `Presentation::stateSemanticKey()`：
    positive / neutral / negative / disabled。
    QML 侧不做任何容器状态字符串比较——状态语义属于 C++（ARCH_V3 §2.1 分层约束）。

    这是本项目里唯一允许出现 Kirigami 状态语义色 token 的地方之一；
    另一处是 ChartPalette（数据可视化，互不引用）。
*/

pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

QtObject {
    /*!
        语义 key → 状态色。用于无法使用徽标的场合（例如卡片左侧的大图标、
        统计卡的数字），徽标本身请使用 StatusChip。
    */
    function color(semanticKey: string): color {
        switch (semanticKey) {
        case "positive":
            return Kirigami.Theme.positiveTextColor;
        case "neutral":
            return Kirigami.Theme.neutralTextColor;
        case "negative":
            return Kirigami.Theme.negativeTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    /*!
        语义 key → Kirigami.Badge.Type。

        注意 Badge 的 Type 名称是**颜色语义**而不是状态语义：
        Positive=绿、Warning=橙、Error=红、Information=中性。
        本映射与 ARCH_V3_pre §1.4 的对照表一致：
        运行中=绿、已暂停=橙、已停止=红、未知/禁用=灰。

        「已停止」用 Error（红）是**颜色约定**，不代表容器处于错误状态；
        状态文字本身始终由 C++ 的 stateText 提供，不由此处派生。
    */
    /*!
     * 区间地图方块的颜色（ARCH_next_ports.md §4.B）。
     *
     * `stateKey` 为空 = 空闲：只用虚线边框，不填色——"空"就应该看起来是空的。
     * 颜色只是辅助：方块里写着端口号，页面还有文字图例。
     */
    function portTileColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveBackgroundColor;
        case "reserved":
            return Kirigami.Theme.neutralBackgroundColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeBackgroundColor;
        case "declaredNotPublished":
            // 用户要求：未占用用**浅蓝**——它原来那档灰和"空闲"几乎分不出来
            // （Kirigami 没有现成的蓝色浅底，用高亮色的低透明度版本）
            return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                           Kirigami.Theme.highlightColor.b, 0.18);
        default:
            return Kirigami.Theme.backgroundColor;
        }
    }

    /*!
     * 方块边框色。强弱顺序（用户要求）：运行中 = 被占用 > 未启动 > 未占用 > 空闲。
     *
     * 空闲是最弱的一档：只留一圈灰边，不填色——"空"看起来就该是空的。
     */
    function portTileBorderColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveTextColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeTextColor;
        case "reserved":
            return Kirigami.Theme.neutralTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    /*! 方块文字色（与边框同一套强弱）。 */
    function portTileTextColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveTextColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeTextColor;
        case "reserved":
            return Kirigami.Theme.neutralTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    function badgeType(semanticKey: string): int {
        switch (semanticKey) {
        case "positive":
            return Kirigami.Badge.Type.Positive;
        case "neutral":
            return Kirigami.Badge.Type.Warning;
        case "negative":
            return Kirigami.Badge.Type.Error;
        default:
            return Kirigami.Badge.Type.Information;
        }
    }

    /*!
        语义 key → 徽标背景的轻量着色，用于统计卡这类「大面积」场合：
        直接使用状态色会让整块卡片过于抢眼，因此只在语义为 positive / negative
        且数值非零时给一层很淡的同色背景（ARCH_V3_pre §1.9「已停止卡片加背景色区分」）。
    */
    function tintColor(semanticKey: string): color {
        switch (semanticKey) {
        case "positive":
            return Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.12);
        case "neutral":
            return Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, 0.12);
        case "negative":
            return Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.12);
        default:
            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);
        }
    }
}
