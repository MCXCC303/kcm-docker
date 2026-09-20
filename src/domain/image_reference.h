/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <optional>

namespace Kontainer::ImageReference
{

/*!
 * Image reference parsing and validation (ARCH_V4 §2.4).
 *
 * Pure functions, no I/O, no Docker dependency, hence unit-testable; the backend uses them to
 * build the pull request's `fromImage` / `tag`, QML for pre-submit input validation
 * (ARCH_V3_pre §2.8 "validate early").
 *
 * Supported forms:
 *   alpine
 *   alpine:3.19
 *   library/alpine
 *   registry.example.com:5000/team/app:1.2.3
 *   alpine@sha256:0123... (digest form; no tag is added then)
 */
struct Parts {
    /*! Explicit registry (not the implicit docker.io); empty when absent. */
    QString registry;
    /*! Repository path (the part after the registry, e.g. library/alpine). */
    QString repository;
    /*! Tag; empty in digest form. */
    QString tag;
    /*! Digest (sha256:...); empty when absent. */
    QString digest;

    /*! `fromImage` parameter: repository (with registry), plus @digest in digest form. */
    QString fromImage() const;
};

/*! Parse failure yields nullopt (empty, whitespace, invalid characters, bad tag/digest). */
std::optional<Parts> parse(const QString &reference);

/*! Whether this is a usable reference (`alpine` is valid; a missing tag means latest). */
bool isValid(const QString &reference);

/*! Normalize: add `latest` when no tag/digest; invalid references are returned unchanged. */
QString normalized(const QString &reference);

/*! Display-only short form: drop the registry prefix (`registry:5000/team/app:1` → `team/app:1`). */
QString shortForm(const QString &reference);

} // namespace Kontainer::ImageReference
