// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Blamdom Launcher - Minecraft Launcher
 *  A fork of Prism Launcher with the Blamdom section: our packs, our mods, our launcher.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "modplatform/blamdom/BlamdomAPI.h"
#include "tasks/Task.h"

class NetJob;

namespace Blamdom {

/**
 * Updates one of our mods in an instance: downloads the catalog jar into a
 * staging file next to the installed one, verifies the sha256 pinned in the
 * catalog, swaps the files via rename (a crash can never lose the old jar),
 * and rewrites the mod's .pw.toml metadata to the new version.
 */
class ModUpdateTask : public Task {
    Q_OBJECT
   public:
    //! oldPath: the installed jar (absolute path). indexDir: the instance's
    //! mods/.index metadata directory.
    ModUpdateTask(const Mod& mod, const QString& oldPath, const QString& indexDir);
    ~ModUpdateTask() override;

    auto mod() const -> const Mod& { return m_mod; }
    auto fileName() const -> QString { return QFileInfo(m_oldPath).fileName(); }

   protected:
    void executeTask() override;

   private:
    Mod m_mod;
    QString m_oldPath;
    QString m_indexDir;
    NetJob* m_job = nullptr;
};

}  // namespace Blamdom
