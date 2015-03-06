/* recent_file_status.cpp
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "recent_file_status.h"

#include <QFileInfo>

// Sigh. The Qt 4 documentation says we should subclass QThread here. Other sources
// insist that we should subclass QObject, then move it to a newly created QThread.
RecentFileStatus::RecentFileStatus(const QString &filename, QObject *parent) :
    QObject(parent), m_filename(filename), m_size(0)
{
}

void RecentFileStatus::start(void) {
    QFileInfo fi;

    fi.setFile(m_filename);

    if (fi.isFile() && fi.isReadable()) {
        emit statusFound(m_filename, fi.size(), true);
    } else {
        emit statusFound(m_filename, 0, false);
    }
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
