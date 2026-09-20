/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/image_reference.h"

#include <QRegularExpression>

namespace Kontainer::ImageReference
{

namespace
{

/*!
 * Whether a reference "looks like" a valid image reference.
 *
 * Deliberately not the full docker/distribution grammar: the goal is to block obviously
 * meaningless input (spaces, URLs, non-ASCII, bare globs), not to overrule the engine — the
 * engine still reports repository names it does not know, and that message is authoritative.
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
    // No whitespace at all (including surrounding spaces — callers should trim first)
    for (const QChar &ch : reference) {
        if (ch.isSpace()) {
            return false;
        }
    }

    const QString withoutDigest = reference.section(QLatin1Char('@'), 0, 0);
    if (!pattern.match(reference).hasMatch()) {
        return false;
    }

    // A colon in registry:port is not a tag separator, so the tag is only the part after the last '/'
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

    // Repository names must be all lowercase (tags may be uppercase, so only the part before the
    // tag is checked): catching this early beats the engine's "repository name must be lowercase"
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

    // A tag can only appear after the last '/' (the registry :port is not a tag)
    const int lastSlash = remaining.lastIndexOf(QLatin1Char('/'));
    const int colon = remaining.indexOf(QLatin1Char(':'), lastSlash + 1);
    if (colon >= 0) {
        parts.tag = remaining.mid(colon + 1);
        remaining = remaining.left(colon);
    }

    // Registry detection follows Docker's rule: first segment contains '.' or ':' or is localhost
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
