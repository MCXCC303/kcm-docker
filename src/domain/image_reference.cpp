/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/image_reference.h"

#include <QRegularExpression>

namespace Kontainer::ImageReference
{

namespace
{

/*!
 * 引用是否「像」一个合法镜像引用。
 *
 * 刻意不使用完整 docker/distribution 语法：这里的目标是挡住明显无意义的输入
 * （空格、URL、中文、裸 glob），而不是替引擎做最终裁决——引擎仍然会把
 * 它不认识的仓库名报回来，那条消息才是权威。
 */
bool looksValid(const QString &reference)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9._:/-]*(@[A-Za-z][A-Za-z0-9]*:[0-9a-fA-F]{32,})?$)"));

    if (reference.isEmpty()) {
        return false;
    }
    if (reference.contains(QLatin1String("://"))) {
        return false;
    }
    // 不允许任何空白（含首尾空格——调用方应先 trim）
    for (const QChar &ch : reference) {
        if (ch.isSpace()) {
            return false;
        }
    }

    const QString withoutDigest = reference.section(QLatin1Char('@'), 0, 0);
    if (!pattern.match(reference).hasMatch()) {
        return false;
    }

    // registry:port 里的冒号不能被当成 tag 分隔符，因此 tag 只取最后一个 '/' 之后的部分
    const int lastSlash = withoutDigest.lastIndexOf(QLatin1Char('/'));
    const QString lastSegment = withoutDigest.mid(lastSlash + 1);
    const int colon = lastSegment.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString tag = lastSegment.mid(colon + 1);
        static const QRegularExpression tagPattern(QStringLiteral(R"(^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$)"));
        if (!tagPattern.match(tag).hasMatch()) {
            return false;
        }
    }

    // 仓库名必须是全小写（tag 允许大写，因此只检查 tag 之前的部分）：
    // 提前挡住，比让引擎返回 "repository name must be lowercase" 更省事
    const QString namePart = colon >= 0 ? withoutDigest.left(withoutDigest.size() - lastSegment.size() + colon) : withoutDigest;
    if (namePart != namePart.toLower()) {
        return false;
    }

    return true;
}

} // namespace

QString Parts::fromImage() const
{
    if (!digest.isEmpty()) {
        return repository + QLatin1Char('@') + digest;
    }
    return repository;
}

std::optional<Parts> parse(const QString &reference)
{
    const QString trimmed = reference.trimmed();
    if (!looksValid(trimmed)) {
        return std::nullopt;
    }

    Parts parts;

    QString remaining = trimmed;
    const int at = remaining.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        parts.digest = remaining.mid(at + 1);
        remaining = remaining.left(at);
    }

    // tag 只可能出现在最后一个 '/' 之后（registry 的 :port 不算 tag）
    const int lastSlash = remaining.lastIndexOf(QLatin1Char('/'));
    const int colon = remaining.indexOf(QLatin1Char(':'), lastSlash + 1);
    if (colon >= 0) {
        parts.tag = remaining.mid(colon + 1);
        remaining = remaining.left(colon);
    }

    // registry 判定沿用 Docker 的规则：第一段含 '.' 或 ':' 或是 localhost
    const int firstSlash = remaining.indexOf(QLatin1Char('/'));
    if (firstSlash > 0) {
        const QString first = remaining.left(firstSlash);
        if (first.contains(QLatin1Char('.')) || first.contains(QLatin1Char(':')) || first == QLatin1String("localhost")) {
            parts.registry = first;
        }
    }

    parts.repository = remaining;
    if (parts.repository.isEmpty()) {
        return std::nullopt;
    }
    if (parts.tag.isEmpty() && parts.digest.isEmpty()) {
        parts.tag = QStringLiteral("latest");
    }
    return parts;
}

bool isValid(const QString &reference)
{
    return parse(reference).has_value();
}

QString normalized(const QString &reference)
{
    const auto parts = parse(reference);
    if (!parts) {
        return reference.trimmed();
    }
    QString result = parts->repository;
    if (!parts->digest.isEmpty()) {
        result += QLatin1Char('@') + parts->digest;
    } else if (!parts->tag.isEmpty()) {
        result += QLatin1Char(':') + parts->tag;
    }
    return result;
}

QString shortForm(const QString &reference)
{
    const QString trimmed = reference.trimmed();
    const int firstSlash = trimmed.indexOf(QLatin1Char('/'));
    if (firstSlash <= 0) {
        return trimmed;
    }
    const QString first = trimmed.left(firstSlash);
    if (first.contains(QLatin1Char('.')) || first.contains(QLatin1Char(':')) || first == QLatin1String("localhost")) {
        return trimmed.mid(firstSlash + 1);
    }
    return trimmed;
}

} // namespace Kontainer::ImageReference
