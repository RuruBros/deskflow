/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

class QString;

namespace deskflow::core {

bool isAllowedDaemonConfigFile(const QString &configFile);

} // namespace deskflow::core
