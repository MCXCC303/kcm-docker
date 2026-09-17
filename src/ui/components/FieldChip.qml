/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    中性信息芯片（ARCH_V4 §2.1）。

    与 StatusChip 的分工必须保持清楚：

      StatusChip  表示**状态**（运行中 / 已停止 / 路径缺失…），颜色来自 StatusPalette
      FieldChip   表示**字段**（bind / volume / tcp / 80/tcp / rw），固定用中性徽标

    混用会让「颜色」这一层信息失去意义：如果端口号也是彩色的，
    用户就没法靠颜色一眼看出哪个容器有问题（§1.1 状态优先于装饰）。

    用法：

        Components.FieldChip { text: "80/tcp" }
        Components.FieldChip { text: i18n("not published"); muted: true }
*/

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.Badge {
    id: chip

    /*!
        芯片文本由基类（`Kirigami.Badge` → `Label`）的 `text` 承载。
        不要在这里重新声明 `property string text`：那会遮蔽基类属性，
        内部渲染用的仍是基类那个（空）属性，结果是一枚没有文字的圆形徽标——
        属性读出来是对的，屏幕上却是错的。
    */

    /*! 弱化显示：用于「未发布」这类补充信息，避免抢走注意。 */
    property bool muted: false

    // 中性：不参与状态语义，因此固定 Information 类型
    type: Kirigami.Badge.Information
    opacity: chip.muted ? 0.7 : 1.0

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text
}
