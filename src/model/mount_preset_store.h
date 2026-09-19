/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container_create_request.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 一条挂载预设（ARCH_V5_V8 §4.1）。
 *
 * 预设有**稳定 id**（不是列表下标）：管理界面里的增删改排序都靠它定位，
 * 否则排序一次就会把"正在编辑哪一条"弄乱。
 */
struct MountPreset {
    QString id;
    /*! bind 的宿主路径 / volume 的卷名；tmpfs 为空。 */
    QString source;
    QString destination;
    /*! bind / volume / tmpfs */
    QString type = QStringLiteral("bind");
    bool readOnly = false;
    /*! 备注（用户自己写的说明，例如"前端配置目录"）。 */
    QString note;
    /*! 收藏：置顶显示。 */
    bool favorite = false;
    /*! 最近一次使用时间（未用过为空）。 */
    QDateTime lastUsedAt;

    /*! 与某个挂载请求是否指向同一处（用于去重与"是否已添加"）。 */
    bool matches(const ContainerMountRequest &request) const;

    friend bool operator==(const MountPreset &lhs, const MountPreset &rhs)
    {
        return lhs.id == rhs.id && lhs.source == rhs.source && lhs.destination == rhs.destination
            && lhs.type == rhs.type && lhs.readOnly == rhs.readOnly && lhs.note == rhs.note
            && lhs.favorite == rhs.favorite && lhs.lastUsedAt == rhs.lastUsedAt;
    }
};

/*!
 * 挂载预设的持久化（`~/.config/kcm_dockerrc`，组 `[MountPresets]`）。
 *
 * 为什么不用 KCM 的配置模块：预设是**这个工具的数据**，不是系统设置（§1.5.3）。
 *
 * 存储形态刻意简单：每条预设一个 `KConfig` 组（`[MountPresets][<id>]`），
 * 加一个记录顺序的 `Order` 键——比把 JSON 塞进一个键更容易用 kreadconfig 之类
 * 的工具排查，也不会因为一个字符坏掉而丢掉全部预设。
 */
class MountPresetStore : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    /*!
     * 预设摘要，做成**属性**而不是只用 Q_INVOKABLE。
     *
     * QML 里 `model: store.summaries()` 是函数调用——**不建立依赖**，Repeater 只在创建时
     * 取一次值，之后新增/删除都不会重铺（实测：预设标签页里看不到已有的预设）。
     * 属性 + NOTIFY 才会跟着变。这个坑在日志、网络、数据卷、预览按钮上已经踩过四次。
     */
    Q_PROPERTY(QVariantList summaries READ summariesProperty NOTIFY changed)

public:
    /*! 最近使用的上限（§4.1）：超出后按时间淘汰最旧的**非收藏**项。 */
    static constexpr int kMaxRecent = 20;

    /*!
     * `configPath` 为空时用 `QStandardPaths` 的 `kcm_dockerrc`。
     *
     * 测试传临时路径即可，不需要污染用户配置。
     */
    explicit MountPresetStore(const QString &configPath = {}, QObject *parent = nullptr);

    int count() const;
    bool empty() const;
    /*! 收藏在前、其余按最近使用时间倒序（都没用过则按加入顺序）。 */
    QList<MountPreset> presets() const;
    /*! 界面用的纯数据摘要 `[{id, source, destination, type, readOnly, note, favorite}]`。 */
    Q_INVOKABLE QVariantList summaries() const;
    /*! 同上，属性形式（QML 的 Repeater 必须用属性）。 */
    QVariantList summariesProperty() const
    {
        return summaries();
    }

    /*! 新增（同宿主 + 同容器路径已存在时返回它的 id，不重复添加）。 */
    Q_INVOKABLE QString add(const QString &source,
                            const QString &destination,
                            const QString &type = QStringLiteral("bind"),
                            bool readOnly = false,
                            const QString &note = {});
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool update(const QString &id,
                            const QString &source,
                            const QString &destination,
                            bool readOnly,
                            const QString &note);
    Q_INVOKABLE bool setFavorite(const QString &id, bool favorite);
    /*! 上移 / 下移（在收藏组内或最近使用组内调整顺序）。 */
    Q_INVOKABLE bool moveUp(const QString &id);
    Q_INVOKABLE bool moveDown(const QString &id);
    /*! 创建成功后调用：把这批挂载并入预设并刷新"最近使用"。 */
    Q_INVOKABLE void noteUsed(const QList<Kontainer::ContainerMountRequest> &mounts);

    /*! 校验（稳定 key）：宿主路径绝对或卷名合法、容器路径绝对、去重。 */
    static QString validateSource(const QString &source, const QString &type);
    static QString validateDestination(const QString &destination);
    /*!
     * 上面两个的**实例版**：QML 只能调用 Q_INVOKABLE / 槽 / 属性，
     * 静态成员函数在 QML 里是 undefined（调用会抛 TypeError）。
     */
    Q_INVOKABLE QString sourceError(const QString &source, const QString &type) const
    {
        return validateSource(source, type);
    }
    Q_INVOKABLE QString destinationError(const QString &destination) const
    {
        return validateDestination(destination);
    }

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    /*! 生成一个不与现有 id 冲突的新 id。 */
    QString nextId() const;
    void trimRecents();

    QString m_configPath;
    QList<MountPreset> m_presets;
    /*! 显式顺序（id 列表）：与 m_presets 的展示顺序一致，保存时写进 `Order`。 */
    QStringList m_order;
};

} // namespace Kontainer
