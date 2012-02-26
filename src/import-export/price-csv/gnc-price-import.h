/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/
/** @file gnc-price-import.h
    @brief CSV import GUI
    @author Copyright (c) 2012 Sebastien Alborini <salborini@gmail.com>
*/
#ifndef PRICE_IMPORT_H
#define PRICE_IMPORT_H

/** The gnc_file_price_import() will let the user select a
 * CSV/Fixed-Width file to open, and import the prices from the file
 * to the price database. It also allows the user to configure how the
 * file is parsed. */
void gnc_file_price_import (void);
#endif
