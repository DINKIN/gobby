/* gobby - A GTKmm driven libobby client
 * Copyright (C) 2005 0x539 dev group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _GOBBY_ENTRYDIALOG_HPP_
#define _GOBBY_ENTRYDIALOG_HPP_

#include <gtkmm/label.h>
#include <gtkmm/entry.h>
#include <gtkmm/dialog.h>

namespace Gobby
{

class EntryDialog: public Gtk::Dialog
{
public:
	EntryDialog(Gtk::Window& parent,
	            const Glib::ustring& title,
	            const Glib::ustring& label);
	virtual ~EntryDialog();

	Glib::ustring get_text() const;
	void set_text(const Glib::ustring& text);

	Gtk::Entry& get_entry();

	void set_check_valid_entry(bool enable);
	bool get_check_valid_entry() const;

protected:
	void on_entry_changed();

	Gtk::Entry m_entry;
	Gtk::Label m_label;
	Gtk::VBox  m_box;

	bool m_check_valid_entry;
};

}

#endif // _GOBBY_ENTRYDIALOG_HPP_