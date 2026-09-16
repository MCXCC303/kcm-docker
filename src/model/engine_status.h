/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/engine_info.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Engine 状态的 presentation model：把 EngineInfo 转为 QML 可绑定的属性。
 * 只做数据承载，不含任何 Docker 访问逻辑。
 */
class EngineStatus : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool countsAvailable READ countsAvailable NOTIFY changed)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY changed)
    Q_PROPERTY(QString apiVersion READ apiVersion NOTIFY changed)
    Q_PROPERTY(QString minApiVersion READ minApiVersion NOTIFY changed)
    Q_PROPERTY(QString osType READ osType NOTIFY changed)
    Q_PROPERTY(QString architecture READ architecture NOTIFY changed)
    Q_PROPERTY(QString kernelVersion READ kernelVersion NOTIFY changed)
    Q_PROPERTY(QString engineName READ engineName NOTIFY changed)
    Q_PROPERTY(QString operatingSystem READ operatingSystem NOTIFY changed)
    Q_PROPERTY(QString cgroupVersion READ cgroupVersion NOTIFY changed)
    Q_PROPERTY(QString storageDriver READ storageDriver NOTIFY changed)
    Q_PROPERTY(int containerTotal READ containerTotal NOTIFY changed)
    Q_PROPERTY(int containersRunning READ containersRunning NOTIFY changed)
    Q_PROPERTY(int containersPaused READ containersPaused NOTIFY changed)
    Q_PROPERTY(int containersStopped READ containersStopped NOTIFY changed)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY changed)

public:
    explicit EngineStatus(QObject *parent = nullptr);

    void setInfo(const EngineInfo &info);
    /*! 清空（Docker 不可用时调用），保证 UI 不展示过期数据。 */
    void clear();

    const EngineInfo &info() const
    {
        return m_info;
    }

    bool available() const
    {
        return m_info.available;
    }
    bool countsAvailable() const
    {
        return m_info.countsAvailable;
    }
    QString serverVersion() const
    {
        return m_info.serverVersion;
    }
    QString apiVersion() const
    {
        return m_info.apiVersion;
    }
    QString minApiVersion() const
    {
        return m_info.minApiVersion;
    }
    QString osType() const
    {
        return m_info.osType;
    }
    QString architecture() const
    {
        return m_info.architecture;
    }
    QString kernelVersion() const
    {
        return m_info.kernelVersion;
    }
    QString engineName() const
    {
        return m_info.engineName;
    }
    QString operatingSystem() const
    {
        return m_info.operatingSystem;
    }
    QString cgroupVersion() const
    {
        return m_info.cgroupVersion;
    }
    QString storageDriver() const
    {
        return m_info.storageDriver;
    }
    int containerTotal() const
    {
        return m_info.containerTotal;
    }
    int containersRunning() const
    {
        return m_info.containersRunning;
    }
    int containersPaused() const
    {
        return m_info.containersPaused;
    }
    int containersStopped() const
    {
        return m_info.containersStopped;
    }
    int imageCount() const
    {
        return m_info.imageCount;
    }

Q_SIGNALS:
    /*! 任一属性变化；QML 绑定会自动重新求值。 */
    void changed();

private:
    EngineInfo m_info;
};

} // namespace Kontainer
