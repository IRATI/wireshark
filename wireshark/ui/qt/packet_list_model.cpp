/* packet_list_model.cpp
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

#include "packet_list_model.h"

#include <epan/epan_dissect.h>
#include <epan/column_info.h>
#include <epan/column.h>
#include <epan/nstime.h>
#include <epan/prefs.h>

#include "color.h"
#include "color_filters.h"

#include "globals.h"

#include "wireshark_application.h"
#include <QColor>

PacketListModel::PacketListModel(QObject *parent, capture_file *cf) :
    QAbstractItemModel(parent)
{
    cap_file_ = cf;
}

void PacketListModel::setCaptureFile(capture_file *cf)
{
    cap_file_ = cf;
}

// Packet list records have no children (for now, at least).
QModelIndex PacketListModel::index(int row, int column, const QModelIndex &parent)
            const
{
    Q_UNUSED(parent);

    if (row >= visible_rows_.count() || row < 0 || !cap_file_ || column >= cap_file_->cinfo.num_cols)
        return QModelIndex();

    PacketListRecord *record = visible_rows_[row];

    return createIndex(row, column, record);
}

// Everything is under the root.
QModelIndex PacketListModel::parent(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return QModelIndex();
}

guint PacketListModel::recreateVisibleRows()
{
    int pos = visible_rows_.count() + 1;
    PacketListRecord *record;

    beginResetModel();
    visible_rows_.clear();
    endResetModel();
    beginInsertRows(QModelIndex(), pos, pos);
    foreach (record, physical_rows_) {
        if (record->getFdata()->flags.passed_dfilter || record->getFdata()->flags.ref_time) {
            visible_rows_ << record;
        }
    }
    endInsertRows();
    return visible_rows_.count();
}

void PacketListModel::setColorEnabled(bool enable_color) {
    enable_color_ = enable_color;
}

void PacketListModel::clear() {
    beginResetModel();
    physical_rows_.clear();
    visible_rows_.clear();
    endResetModel();
}

int PacketListModel::rowCount(const QModelIndex &parent) const
{
    if (!cap_file_) return 0;

    if (parent.column() >= cap_file_->cinfo.num_cols)
        return 0;

    return visible_rows_.count();
}

int PacketListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    if (!cap_file_) return 0;

    return cap_file_->cinfo.num_cols;
}

QVariant PacketListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    PacketListRecord *record = static_cast<PacketListRecord*>(index.internalPointer());
    if (!record)
        return QVariant();
    frame_data *fdata = record->getFdata();
    if (!fdata)
        return QVariant();

    switch (role) {
    case Qt::FontRole:
        return wsApp->monospaceFont();
//    case Qt::TextAlignmentRole:
    case Qt::BackgroundRole:
        const color_t *color;
        if (fdata->flags.ignored) {
            color = &prefs.gui_ignored_bg;
        } else if (fdata->flags.marked) {
            color = &prefs.gui_marked_bg;
        } else if (fdata->color_filter) {
            const color_filter_t *color_filter = (const color_filter_t *) fdata->color_filter;
            color = &color_filter->bg_color;
        } else {
            return QVariant();
        }
//        g_log(NULL, G_LOG_LEVEL_DEBUG, "i: %d m: %d cf: %p bg: %d %d %d", fdata->flags.ignored, fdata->flags.marked, fdata->color_filter, color->red, color->green, color->blue);
        return QColor(color->red >> 8, color->green >> 8, color->blue >> 8);
    case Qt::ForegroundRole:
        if (fdata->flags.ignored) {
            color = &prefs.gui_ignored_fg;
        } else if (fdata->flags.marked) {
            color = &prefs.gui_marked_fg;
        } else if (fdata->color_filter) {
            const color_filter_t *color_filter = (const color_filter_t *) fdata->color_filter;
            color = &color_filter->fg_color;
        } else {
            return QVariant();
        }
        return QColor(color->red >> 8, color->green >> 8, color->blue >> 8);
    case Qt::DisplayRole:
        // Fall through
        break;
    default:
        return QVariant();
    }

    int col_num = index.column();
//    g_log(NULL, G_LOG_LEVEL_DEBUG, "showing col %d", col_num);

    if (!cap_file_ || col_num > cap_file_->cinfo.num_cols)
        return QVariant();

    epan_dissect_t edt;
    column_info *cinfo;
    gboolean create_proto_tree;
    struct wtap_pkthdr phdr; /* Packet header */
    guint8 pd[WTAP_MAX_PACKET_SIZE];  /* Packet data */
    gboolean dissect_columns = TRUE; // XXX - Currently only a placeholder

    if (dissect_columns && cap_file_)
        cinfo = &cap_file_->cinfo;
    else
        cinfo = NULL;

    if (!cap_file_ || !cf_read_frame_r(cap_file_, fdata, &phdr, pd)) {
        /*
         * Error reading the frame.
         *
         * Don't set the color filter for now (we might want
         * to colorize it in some fashion to warn that the
         * row couldn't be filled in or colorized), and
         * set the columns to placeholder values, except
         * for the Info column, where we'll put in an
         * error message.
         */
        if (dissect_columns) {
            col_fill_in_error(cinfo, fdata, FALSE, FALSE /* fill_fd_columns */);

            //            for(gint col = 0; col < cinfo->num_cols; ++col) {
            //                /* Skip columns based on frame_data because we already store those. */
            //                if (!col_based_on_frame_data(cinfo, col))
            //                    packet_list_change_record(packet_list, record->physical_pos, col, cinfo);
            //            }
            //            record->columnized = TRUE;
        }
        if (enable_color_) {
            fdata->color_filter = NULL;
            //            record->colorized = TRUE;
        }
        return QVariant();	/* error reading the frame */
    }

    create_proto_tree = (color_filters_used() && enable_color_) ||
                        (have_custom_cols(cinfo) && dissect_columns);

    epan_dissect_init(&edt,
                      create_proto_tree,
                      FALSE /* proto_tree_visible */);

    if (enable_color_)
        color_filters_prime_edt(&edt);
    if (dissect_columns)
        col_custom_prime_edt(&edt, cinfo);

    epan_dissect_run(&edt, &phdr, pd, fdata, cinfo);

    if (enable_color_)
        fdata->color_filter = color_filters_colorize_packet(&edt);

    if (dissect_columns) {
        /* "Stringify" non frame_data vals */
        epan_dissect_fill_in_columns(&edt, FALSE, FALSE /* fill_fd_columns */);

        //            for(col = 0; col < cinfo->num_cols; ++col) {
        //                    /* Skip columns based on frame_data because we already store those. */
        //                    if (!col_based_on_frame_data(cinfo, col))
        //                            packet_list_change_record(packet_list, record->physical_pos, col, cinfo);
        //            }
//        g_log(NULL, G_LOG_LEVEL_DEBUG, "d_c %d: %s", col_num, cinfo->col_data[col_num]);
    }

    //    if (dissect_columns)
    //            record->columnized = TRUE;
    //    if (enable_color_)
    //            record->colorized = TRUE;

    epan_dissect_cleanup(&edt);

    return record->data(col_num, cinfo);
}

QVariant PacketListModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (!cap_file_) return QVariant();

    if (orientation == Qt::Horizontal && section < cap_file_->cinfo.num_cols) {
        switch (role) {
        case Qt::DisplayRole:
            return cap_file_->cinfo.col_title[section];
        default:
            break;
        }
    }

    return QVariant();
}

gint PacketListModel::appendPacket(frame_data *fdata)
{
    PacketListRecord *record = new PacketListRecord(fdata);
    gint pos = visible_rows_.count() + 1;

    physical_rows_ << record;

    if (fdata->flags.passed_dfilter || fdata->flags.ref_time) {
        beginInsertRows(QModelIndex(), pos, pos);
        visible_rows_ << record;
        endInsertRows();
    } else {
        pos = -1;
    }
    return pos;
}

frame_data *PacketListModel::getRowFdata(int row) {
    if (row < 0 || row >= visible_rows_.size())
        return NULL;
    PacketListRecord *record = visible_rows_[row];
    if (!record)
        return NULL;
    return record->getFdata();
}

int PacketListModel::visibleIndexOf(frame_data *fdata) const
{
    int row = 0;
    foreach (PacketListRecord *record, visible_rows_) {
        if (record->getFdata() == fdata) {
            return row;
        }
        row++;
    }

    return -1;
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
