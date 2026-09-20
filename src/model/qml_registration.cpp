/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/qml_registration.h"

#include "model/container_detail_controller.h"
#include "model/container_filter_model.h"
#include "model/container_model.h"
#include "model/daemon_config_controller.h"
#include "model/detail_list_model.h"
#include "model/engine_status.h"
#include "model/format.h"
#include "model/image_detail_controller.h"
#include "model/image_filter_model.h"
#include "model/image_model.h"
#include "model/network_filter_model.h"
#include "model/network_detail_controller.h"
#include "model/network_model.h"
#include "model/volume_filter_model.h"
#include "model/volume_model.h"
#include "model/metrics_model.h"
#include "model/operation_controller.h"
#include "model/presentation.h"
#include "model/status_controller.h"
#include "model/storage_status.h"

#include <QQmlEngine>
#include <QString>

namespace Kontainer
{

void registerKontainerQmlTypes()
{
    qmlRegisterUncreatableType<StatusController>("org.kde.kcm.docker", 1, 0, "StatusController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ContainerModel>("org.kde.kcm.docker", 1, 0, "ContainerModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ImageModel>("org.kde.kcm.docker", 1, 0, "ImageModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<EngineStatus>("org.kde.kcm.docker", 1, 0, "EngineStatus", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<StorageStatus>("org.kde.kcm.docker", 1, 0, "StorageStatus", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ContainerFilterModel>("org.kde.kcm.docker", 1, 0, "ContainerFilterModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ImageFilterModel>("org.kde.kcm.docker", 1, 0, "ImageFilterModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<NetworkModel>("org.kde.kcm.docker", 1, 0, "NetworkModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<NetworkFilterModel>("org.kde.kcm.docker", 1, 0, "NetworkFilterModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<VolumeModel>("org.kde.kcm.docker", 1, 0, "VolumeModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<CommandHistoryStore>("org.kde.kcm.docker", 1, 0, "CommandHistoryStore", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<CreateContainerController>("org.kde.kcm.docker", 1, 0, "CreateContainerController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<DirectoryPicker>("org.kde.kcm.docker", 1, 0, "DirectoryPicker", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<MountPresetStore>("org.kde.kcm.docker", 1, 0, "MountPresetStore", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<VolumeDetailController>("org.kde.kcm.docker", 1, 0, "VolumeDetailController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<VolumeFilterModel>("org.kde.kcm.docker", 1, 0, "VolumeFilterModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<NetworkDetailController>("org.kde.kcm.docker", 1, 0, "NetworkDetailController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<DetailListModel>("org.kde.kcm.docker", 1, 0, "DetailListModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<MetricsModel>("org.kde.kcm.docker", 1, 0, "MetricsModel", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ContainerDetailController>("org.kde.kcm.docker", 1, 0, "ContainerDetailController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<DaemonConfigController>("org.kde.kcm.docker", 1, 0, "DaemonConfigController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<OperationController>("org.kde.kcm.docker", 1, 0, "OperationController", QStringLiteral("Provided by the KCM"));
    qmlRegisterUncreatableType<ImageDetailController>("org.kde.kcm.docker", 1, 0, "ImageDetailController", QStringLiteral("Provided by the KCM"));

    qmlRegisterSingletonType<Format>("org.kde.kcm.docker", 1, 0, "Format", [](QQmlEngine *, QJSEngine *) -> QObject * {
        return new Format;
    });
    qmlRegisterSingletonType<Presentation>("org.kde.kcm.docker", 1, 0, "Presentation", [](QQmlEngine *, QJSEngine *) -> QObject * {
        return new Presentation;
    });
}

} // namespace Kontainer
