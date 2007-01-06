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

#include <cerrno>
#include <stdexcept>
#include <fstream>
#include <ostream>

#include <glibmm/miscutils.h>
#include <gtkmm/main.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/stock.h>

#include <obby/format_string.hpp>
#include <obby/client_buffer.hpp>
#include <obby/host_buffer.hpp>

#include "common.hpp"
#include "encoding.hpp"
#include "docwindow.hpp"
#include "passworddialog.hpp"
#include "entrydialog.hpp"
#include "preferencesdialog.hpp"
#include "joindialog.hpp"
#include "hostdialog.hpp"
#include "joinprogressdialog.hpp"
#include "hostprogressdialog.hpp"
#include "window.hpp"
#include "features.hpp"
#include "icon.hpp"
#include "colorsel.hpp"

Gobby::Window::Window(const IconManager& icon_mgr, Config& config)
 : Gtk::Window(Gtk::WINDOW_TOPLEVEL),
   m_config(config),
   m_preferences(m_config), m_icon_mgr(icon_mgr),
   m_buffer(NULL),
#ifdef WITH_HOWL
   m_zeroconf(NULL),
#endif
   m_header(),
   m_userlist(*this, m_header, m_preferences, config["windows"]),
   m_documentlist(*this, m_header, m_preferences, config["windows"]),
   m_finddialog(*this), m_gotodialog(*this),
   m_folder(m_header, m_preferences),
   m_statusbar(m_header, m_folder)
{
	// Header
	m_header.action_app_session_create->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_session_create) );
	m_header.action_app_session_join->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_session_join) );
	m_header.action_app_session_save->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_session_save) );
	m_header.action_app_session_quit->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_session_quit) );
	m_header.action_app_quit->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_quit) );

	m_header.action_session_document_create->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_document_create) );
	m_header.action_session_document_open->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_document_open) );
	m_header.action_session_document_save->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_document_save) );
	m_header.action_session_document_save_as->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_document_save_as) );
	m_header.action_session_document_close->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_document_close) );

	m_header.action_edit_search->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_edit_search) );
	m_header.action_edit_search_replace->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_edit_search_replace) );
	m_header.action_edit_goto_line->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_edit_goto_line) );
	m_header.action_edit_preferences->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_edit_preferences) );

	m_header.action_user_set_password->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_user_set_password) );
	m_header.action_user_set_colour->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_user_set_colour) );
	
	m_header.action_view_preferences->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_view_preferences) );

	m_header.action_help_about->signal_activate().connect(
		sigc::mem_fun(*this, &Window::on_about) );

	// Folder
	m_folder.document_add_event().connect(
		sigc::mem_fun(*this, &Window::on_folder_document_add) );
	m_folder.document_remove_event().connect(
		sigc::mem_fun(*this, &Window::on_folder_document_remove) );
	m_folder.document_close_request_event().connect(
		sigc::mem_fun(*this, &Window::on_folder_document_close_request) );
	m_folder.tab_switched_event().connect(
		sigc::mem_fun(*this, &Window::on_folder_tab_switched) );

	// Build UI
	add_accel_group(m_header.get_accel_group() );

	// Initialise header sensitivity
	m_header.group_session->set_sensitive(false);
	m_header.group_user->set_sensitive(false);
	m_header.group_view->set_sensitive(false);
	m_header.action_app_session_save->set_sensitive(false);
	m_header.action_app_session_quit->set_sensitive(false);

	m_frame_chat.set_shadow_type(Gtk::SHADOW_IN);
	m_frame_text.set_shadow_type(Gtk::SHADOW_IN);

	m_frame_chat.add(m_chat);
	m_frame_text.add(m_folder);

	m_mainpaned.set_border_width(10);
	m_mainpaned.pack1(m_frame_text, true, false);
	m_mainpaned.pack2(m_frame_chat, true, false);

	m_mainbox.pack_start(m_header, Gtk::PACK_SHRINK);
	m_mainbox.pack_start(m_mainpaned, Gtk::PACK_EXPAND_WIDGET);
	m_mainbox.pack_start(m_statusbar, Gtk::PACK_SHRINK);

	add(m_mainbox);

	// Apply initial preferences
	apply_preferences();

	set_title("Gobby");
	set_default_size(640, 480);

#ifdef WITH_HOWL
	// Initialise Zeroconf
	try
	{
		m_zeroconf.reset(new obby::zeroconf);
	}
	catch(std::runtime_error&)
	{
		display_error(_("Howl initialisation failed. Probably you need "
			"to run mDNSResponder as root prior to Gobby. "
			"Zeroconf support is deactivated for this session."),
			Gtk::MESSAGE_WARNING);
		m_zeroconf.reset();
	}
#endif

	if(m_preferences.appearance.remember)
	{
		// Restore the window's position from the configuration
        	const int x = config["windows"]["main"]["x"].get<int>(0);
	        const int y = config["windows"]["main"]["y"].get<int>(0);
		const int w = config["windows"]["main"]["width"].get<int>(0);
		const int h = config["windows"]["main"]["height"].get<int>(0);
		const int s_w = config["screen"]["width"].get<int>(0);
		const int s_h = config["screen"]["height"].get<int>(0);
		bool first_run = (x == 0 && y == 0 && w == 0 && h == 0);

		Glib::RefPtr<Gdk::Screen> screen(get_screen() );
		if((screen->get_width() >= s_w && screen->get_height() >= s_h)
			&& !first_run)
		{
			move(x, y);
			resize(w, h);
		}
	}
}

Gobby::Window::~Window()
{
	obby_end();

	// Serialise preferences into config
	m_preferences.serialise(m_config);

	// Save the window's current position
	if(m_preferences.appearance.remember)
	{
		int x, y, w, h;
		get_position(x, y); get_size(w, h);
		Glib::RefPtr<Gdk::Screen> screen(get_screen() );

		m_config["windows"]["main"]["x"].set(x);
		m_config["windows"]["main"]["y"].set(y);
		m_config["windows"]["main"]["width"].set(w);
		m_config["windows"]["main"]["height"].set(h);
		m_config["screen"]["width"].set(screen->get_width() );
		m_config["screen"]["height"].set(screen->get_height() );
	}
}

void Gobby::Window::on_realize()
{
	Gtk::Window::on_realize();

	// Initialize paned sizes. This cannot be done in the constructor
	// because the widget's sizes are not known.
	int mainmin = m_mainpaned.property_min_position();
	int mainmax = m_mainpaned.property_max_position();

	m_mainpaned.set_position(mainmin + (mainmax - mainmin) * 3 / 4);
}

bool Gobby::Window::on_delete_event(GdkEventAny* event)
{
	if(m_buffer.get() == NULL) return false;

	Gtk::MessageDialog dlg(
		*this,
		_("You are still connected to a session"),
		false,
		Gtk::MESSAGE_WARNING,
		Gtk::BUTTONS_NONE,
		true
	);

	dlg.set_secondary_text(
		_("Do you want to close Gobby nevertheless?")
	);

	dlg.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	dlg.add_button(Gtk::Stock::CLOSE, Gtk::RESPONSE_YES)->grab_focus();

	return dlg.run() != Gtk::RESPONSE_YES;
}

void Gobby::Window::obby_start()
{
	// Connect to obby events
	m_buffer->user_join_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_user_join) );
	m_buffer->user_part_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_user_part) );
	m_buffer->user_colour_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_user_colour) );
	m_buffer->user_colour_failed_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_user_colour_failed) );

	m_buffer->document_insert_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_document_insert) );
	m_buffer->document_remove_event().connect(
		sigc::mem_fun(*this, &Window::on_obby_document_remove) );

	// Accept drag and drop of files into the gobby window
	m_dnd.reset(new DragDrop(*this) );

	m_header.group_session->set_sensitive(true);
	m_header.action_session_document_create->set_sensitive(true);
	m_header.action_session_document_open->set_sensitive(true);
	m_header.group_user->set_sensitive(true);
	m_header.group_view->set_sensitive(true);
	m_header.action_app_session_save->set_sensitive(true);
	m_header.action_app_session_quit->set_sensitive(true);

	m_header.action_app_session_create->set_sensitive(false);
	m_header.action_app_session_join->set_sensitive(false);

	m_header.action_user_set_password->set_sensitive(
		dynamic_cast<obby::client_buffer*>(m_buffer.get()) != NULL
	);

	// Delegate start of obby session
	m_folder.obby_start(*m_buffer);
	m_userlist.obby_start(*m_buffer);
	m_documentlist.obby_start(*m_buffer);
	m_chat.obby_start(*m_buffer);
	m_statusbar.obby_start(*m_buffer);

	// Forward user joins
	const obby::user_table& table = m_buffer->get_user_table();
	for(obby::user_table::iterator iter =
		table.begin(obby::user::flags::NONE, obby::user::flags::NONE);
	    iter != table.end(obby::user::flags::NONE, obby::user::flags::NONE);
	    ++ iter)
	{
		on_obby_user_join(*iter);
	}

	// Send documents to components
	obby::buffer::document_iterator iter = m_buffer->document_begin();
	for(; iter != m_buffer->document_end(); ++ iter)
		on_obby_document_insert(*iter);

	// Set last page as active one because it is currently shown anyway.
	if(m_buffer->document_count() > 0)
		m_folder.set_current_page(m_buffer->document_count() - 1);

	// Clear location of previous session file, this is a new session
	m_prev_session = "";

	// Current document has changed, update titlebar
	update_title_bar();

	// Show up document list if obby buffer contains documents
	if(m_buffer->document_count() > 0)
	{
		m_documentlist.show();
		m_documentlist.grab_focus();
	}
}

void Gobby::Window::obby_end()
{
	// Nothing to do if no buffer is open
	if(!m_buffer.get() ) return;

	// Remove DND handler
	m_dnd.reset(NULL);

	// Tell GUI components that the session ended
	m_folder.obby_end();
	m_userlist.obby_end();
	m_documentlist.obby_end();
	m_chat.obby_end();
	m_statusbar.obby_end();

	m_header.action_session_document_create->set_sensitive(false);
	m_header.action_session_document_open->set_sensitive(false);
	m_header.group_user->set_sensitive(false);
	m_header.action_app_session_save->set_sensitive(false);
	m_header.action_app_session_quit->set_sensitive(false);

	m_header.action_app_session_create->set_sensitive(true);
	m_header.action_app_session_join->set_sensitive(true);

	// Delete buffer
	m_buffer.reset();

#ifdef WITH_HOWL
	if(m_zeroconf.get() )
		m_zeroconf->unpublish_all();
#endif
}

void Gobby::Window::on_session_create()
{
	// Show up host dialog
	HostDialog dlg(*this, m_config);
	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		dlg.hide();

		// Read setting
		unsigned int port = dlg.get_port();
		Glib::ustring name = dlg.get_name();
		Gdk::Color color = dlg.get_color();	
		Glib::ustring password = dlg.get_password();
		Glib::ustring session = dlg.get_session();

		// Set up host with hostprogressdialog
		HostProgressDialog prgdlg(
			*this, m_config, port, name, color, session);
		if(prgdlg.run() == Gtk::RESPONSE_OK)
		{
			prgdlg.hide();

			// Get buffer
			std::auto_ptr<obby::host_buffer> buffer =
				prgdlg.get_buffer();

			// Set password
			buffer->set_global_password(password);
#ifdef WITH_HOWL
			// Publish the newly created session via Zeroconf
			// if Howl is not deactivated
			if(m_zeroconf.get() )
				m_zeroconf->publish(name, port);
#endif

			obby::format_string str(_("Serving on port %0%") );
			str << port;
			m_statusbar.update_connection(str.str() );

			m_buffer = buffer;

			// Start session
			obby_start();
			// Remember session file
			m_prev_session = Glib::filename_from_utf8(session);
		}
	}
}

void Gobby::Window::on_session_join()
{
#ifndef WITH_HOWL
	JoinDialog dlg(*this, m_config);
#else
	JoinDialog dlg(*this, m_config, m_zeroconf.get() );
#endif

	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		dlg.hide();

		// Read settings
		Glib::ustring host = dlg.get_host();
		unsigned int port = dlg.get_port();
		Glib::ustring name = dlg.get_name();
		Gdk::Color color = dlg.get_color();

		JoinProgressDialog prgdlg(
			*this, m_config, host, port, name, color);
		if(prgdlg.run() == Gtk::RESPONSE_OK)
		{
			prgdlg.hide();

			// Get buffer
			std::auto_ptr<obby::client_buffer> buffer =
				prgdlg.get_buffer();

			buffer->close_event().connect(
				sigc::mem_fun(*this, &Window::on_obby_close) );

			obby::format_string str(_("Connected to %0%:%1%") );
			str << host << port;
			m_statusbar.update_connection(str.str() );

			// Start session
			m_buffer = buffer;
			obby_start();
		}
	}
}

void Gobby::Window::on_session_save()
{
	Gtk::CheckButton m_chk_default_ext(
		_("Use default .obby extension if none is given")
	);

	Gtk::FileChooserDialog dlg(
		*this,
		_("Save obby session"),
		Gtk::FILE_CHOOSER_ACTION_SAVE
	);

	m_chk_default_ext.set_active(true);
	dlg.get_vbox()->pack_start(m_chk_default_ext, Gtk::PACK_SHRINK);
	// This option confuses the overwrite confirmation :/
//	m_chk_default_ext.show();

#ifdef GTKMM_GEQ_28
	dlg.set_do_overwrite_confirmation(true);
#endif

	// Use the location of a previously saved session, if any
	if(!m_prev_session.empty() )
	{
		dlg.set_filename(m_prev_session);
	}
	else
	{
		// Use the last used path for this dialog, if we have any
		if(!m_last_path.empty() )
			dlg.set_current_folder(m_last_path);
	}

	dlg.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	dlg.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);

	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// Use current folder as standard folder for other dialogs
		m_last_path = dlg.get_current_folder();
		// Get selected filename
		std::string filename = dlg.get_filename();

		// Append .obby extension if none is given
		/*if(m_chk_default_ext.get_active() )
			if(filename.find('.') == std::string::npos)
				filename += ".obby";*/

		// Save document
		try
		{
			m_buffer->serialise(filename);
			m_prev_session = filename;
		}
		catch(std::exception& e)
		{
			display_error(e.what() );
		}
	}
}

void Gobby::Window::on_session_quit()
{
	obby_end();
}

void Gobby::Window::on_about()
{
	Gtk::AboutDialog dlg;
	dlg.set_name("Gobby");
	dlg.set_version(PACKAGE_VERSION);
	dlg.set_comments(_("A collaborative text editor"));
	dlg.set_copyright("Copyright (C) 2005 0x539 dev group <crew@0x539.de>");
	dlg.set_logo(m_icon_mgr.gobby);

	std::deque<Glib::ustring> authors;
	authors.push_back("Developers:");
	authors.push_back("  Armin Burgmeier <armin@0x539.de>");
	authors.push_back("  Philipp Kern <phil@0x539.de>");
	authors.push_back("");
	authors.push_back("Contributors:");
	authors.push_back("  Benjamin Herr <ben@0x539.de>");
	
	std::deque<Glib::ustring> artists;
	artists.push_back("Thomas Glatt <tom@0x539.de>");

	dlg.set_authors(authors);
	dlg.set_artists(artists);

	dlg.set_translator_credits(
		"French translation:\n"
		"  Peer Janssen <peer@baden-online.de>\n"
		"  Mohammed Adnene Trojette <adn@diwi.org>\n"
		"\n"
		"Spanish translation:\n"
		"  Mario Palomo Torrero <mpalomo@ihman.com>\n"
		"\n"
		"Swedish translation:\n"
		"  Daniel Nylander <po@danielnylander.se>\n"
		"\n"
		"German translation:\n"
		"  Philipp Kern <phil@0x539.de>\n"
		"  Armin Burgmeier <armin@0x539.de>\n"
		"\n"
		"Please help in translating this release using Rosetta:\n"
		"\n"
		"After you create your account on https://launchpad.net\n"
		"you could access the translation template on\n"
		"https://launchpad.net/products/gobby/+series/0.3/+pots/gobby/"
		"\nwhere you could translate it directly into your favourite\n"
		"language. Your help is much appreciated!"
		);

	dlg.set_license(
		"This program is free software; you can redistribute it\n"
		"and/or modify it under the terms of the GNU General Public\n"
		"License as published by the Free Software Foundation; either\n"
		"version 2 of the License, or (at your option) any later\n"
		"version.\n"
		"\n"
		"This program is distributed in the hope that it will be\n"
		"useful, but WITHOUT ANY WARRANTY; without even the implied\n"
		"warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR\n"
		"PURPOSE.  See the GNU General Public License for more details."
	);
	dlg.run();
}

void Gobby::Window::on_folder_document_add(DocWindow& window)
{
	const obby::document_info& document =
		window.get_document().get_document();

	// Set the path from which this document was opened, if we opened that
	// file.
	if(document.get_owner() == &m_buffer->get_self() &&
	   !m_local_file_path.empty() )
	{
		// " " is newly created, so we do not need a path
		if(m_local_file_path != " ")
			window.get_document().set_path(m_local_file_path);

		// Crear local path
		m_local_file_path.clear();
	}

	// Select newly created page
	m_folder.set_current_page(m_folder.page_num(window) );
	window.get_document().grab_focus();
}

void Gobby::Window::on_folder_document_remove(DocWindow& window)
{
	// Update title bar if there are no more documents left
	// (folder_tab_switched is not emitted in this case)
	if(m_folder.get_n_pages() == 0)
	{
		update_title_bar();

		// TODO: This should be done automatically with flags for the
		// menu items which specify when the entry is visible
		if(m_buffer.get() == NULL)
		{
			m_header.group_session->set_sensitive(false);
			m_header.group_view->set_sensitive(false);
		}
	}
}

void Gobby::Window::on_folder_document_close_request(Document& document)
{
	// TODO: Folder should emit signal with DocWindow
	for(int i = 0; i < m_folder.get_n_pages(); ++ i)
	{
		Gtk::Widget* doc = m_folder.get_nth_page(i);
		if(&static_cast<DocWindow*>(doc)->get_document() == &document)
		{
			// Close this document
			close_document(*static_cast<DocWindow*>(doc));
			break;
		}
	}
}

void Gobby::Window::on_folder_tab_switched(Document& document)
{
	// Update title bar
	update_title_bar();
}

void Gobby::Window::on_document_create()
{
	EntryDialog dlg(*this, _("Create document"), _("Enter document name"));
	dlg.set_check_valid_entry(true);

	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// " " means a newly created file
		m_local_file_path = " ";
		// Create new document
		m_buffer->document_create(dlg.get_text(), "");
	}
}

void Gobby::Window::on_document_open()
{
	// Create FileChooser
	Gtk::FileChooserDialog dlg(*this, _("Open new document"));

	// Use the last used path for this dialog, if we have any
	if(!m_last_path.empty() )
		dlg.set_current_folder(m_last_path);

	// Create buttons to close it
	dlg.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	dlg.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);

	// Allow multi selection
	dlg.set_select_multiple(true);

	// Show FileChooser
	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// Use current folder as standard folder for later dialogs
		m_last_path = dlg.get_current_folder();
		// Open chosen files
		std::list<Glib::ustring> list = dlg.get_filenames();
		for(std::list<Glib::ustring>::iterator iter = list.begin();
		    iter != list.end();
		    ++ iter)
			open_local_file(*iter);
	}
}

void Gobby::Window::on_document_save()
{
	// Get page
	Document* doc = get_current_document();
	if(doc == NULL)
		throw std::logic_error("Gobby::Window::on_document_save");

	// Is there already a path for this document?
	if(!doc->get_path().empty() )
		// Yes, so save the document there
		save_local_file(*doc, doc->get_path() );
	else
		// Open save as dialog otherwise
		on_document_save_as();
}

void Gobby::Window::on_document_save_as()
{
	// Get page
	Document* doc = get_current_document();
	if(doc == NULL)
		throw std::logic_error("Gobby::Window::on_document_save_as");

	// Setup dialog
	Gtk::FileChooserDialog dlg(*this, _("Save current document"),
		Gtk::FILE_CHOOSER_ACTION_SAVE);

#ifdef GTKMM_GEQ_28
	dlg.set_do_overwrite_confirmation(true);
#endif

	// Does the document have already a path?
	if(!doc->get_path().empty() )
	{
		// Yes, so set it as filename
		dlg.set_filename(doc->get_path() );
	}
	else
	{
		// No, so use the last path a filesel dialog was closed with
		if(!m_last_path.empty() )
			dlg.set_current_folder(m_last_path);
		// Set current title as proposed file name
		dlg.set_current_name(doc->get_title() );
	}

	// Add buttons to close the dialog
	dlg.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	dlg.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);

	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// Use current folder as standard folder for other dialogs
		m_last_path = dlg.get_current_folder();
		// Save document
		save_local_file(*doc, dlg.get_filename() );
	}
}

void Gobby::Window::on_document_close()
{
	// Get current page
	Widget* page = m_folder.get_nth_page(m_folder.get_current_page() );
	// Close it
	close_document(*static_cast<DocWindow*>(page) );
}

void Gobby::Window::on_edit_search()
{
	m_finddialog.set_search_only(true);
	m_finddialog.show();
	m_finddialog.grab_focus();
}

void Gobby::Window::on_edit_search_replace()
{
	m_finddialog.set_search_only(false);
	m_finddialog.show();
	m_finddialog.grab_focus();
}

void Gobby::Window::on_edit_goto_line()
{
	m_gotodialog.show();
	m_gotodialog.grab_focus();
}

void Gobby::Window::on_edit_preferences()
{
	PreferencesDialog dlg(*this, m_preferences, false);

	// Info label
	Gtk::Label m_lbl_info(_(
		"Click on \"Apply\" to apply the new settings to documents "
		"that are currently open. \"OK\" will just store the values "
		"to use them with newly created documents."
	) );

	// Show info label and apply button if documents are open
	if(m_buffer.get() && m_buffer->document_count() > 0)
	{
		m_lbl_info.set_line_wrap(true);
		dlg.get_vbox()->pack_start(m_lbl_info, Gtk::PACK_SHRINK);
		dlg.add_button(Gtk::Stock::APPLY, Gtk::RESPONSE_APPLY);
		m_lbl_info.show();
	}

	int result = dlg.run();
	if(result == Gtk::RESPONSE_OK || result == Gtk::RESPONSE_APPLY)
	{
		// Use new preferences
		m_preferences = dlg.preferences();

		// Apply window preferences
		apply_preferences();

		// Apply preferences to open documents.
		if(result == Gtk::RESPONSE_APPLY)
		{
			for(int i = 0; i < m_folder.get_n_pages(); ++ i)
			{
				DocWindow& doc = *static_cast<DocWindow*>(
					m_folder.get_nth_page(i) );
				doc.get_document().set_preferences(
					m_preferences);
			}
		}
	}
}

void Gobby::Window::on_user_set_password()
{
	// Build password dialog with info
	PasswordDialog dlg(*this, _("Set user password"), false);
	dlg.set_info(_(
		"Set a user password for your user account. When you try to "
		"login next time with this user, you will be prompted for your "
		"password."
	) );

	// Run it
	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		dynamic_cast<obby::client_buffer*>(
			m_buffer.get() )->set_password(dlg.get_password() );
	}
}

void Gobby::Window::on_user_set_colour()
{
	// Simple ColorSelectionDialog
	ColorSelectionDialog dlg;
	const obby::user& user = m_buffer->get_self();
	Gdk::Color color;

	color.set_red(user.get_colour().get_red() * 65535 / 255);
	color.set_green(user.get_colour().get_green() * 65535 / 255);
	color.set_blue(user.get_colour().get_blue() * 65535 / 255);
	dlg.get_colorsel()->set_current_color(color);

	// Run it
	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// Convert GDK color to obby color, set new color
		Gdk::Color color = dlg.get_colorsel()->get_current_color();
		m_buffer->set_colour(
			obby::colour(
				color.get_red() * 255 / 65535,
				color.get_green() * 255 / 65535,
				color.get_blue() * 255 / 65535
			)
		);
	}
}

void Gobby::Window::on_view_preferences()
{
	// Get current page
	Document* doc = get_current_document();
	if(doc == NULL)
		throw std::logic_error("Gobby::Window::on_view_preferences");

	// Add preferences dialog
	PreferencesDialog dlg(*this, doc->get_preferences(), true);

	// Label text
	obby::format_string str(_(
		"These preferences affect only the currently active document "
		"\"%0%\". If you want to change global preferences, use the "
		"preferences menu item in the \"Edit\" menu."
	) );

	// Get title
	str << doc->get_title();

	// Info label
	Gtk::Label m_lbl_info(str.str() );
	m_lbl_info.set_line_wrap(true);

	// Add it into the dialog
	dlg.get_vbox()->pack_start(m_lbl_info, Gtk::PACK_SHRINK);
	dlg.get_vbox()->reorder_child(m_lbl_info, 0); // Push to top of dialog
	m_lbl_info.show();

	// Show the dialog
	if(dlg.run() == Gtk::RESPONSE_OK)
	{
		// Apply new preferences to the document
		doc->set_preferences(dlg.preferences() );
	}
}

void
Gobby::Window::on_view_language(const Glib::RefPtr<Gtk::SourceLanguage>& lang)
{
	// Set language of current document
	Document* doc = get_current_document();
	if(doc == NULL)
		throw std::logic_error("Gobby::Window::on_view_language");

	doc->set_language(lang);
}

void Gobby::Window::on_quit()
{
	if(on_delete_event(NULL) == false)
	{
		// Quit session
		obby_end();
		// End program
		Gtk::Main::quit();
	}
}

void Gobby::Window::on_obby_close()
{
	display_error(_("Connection lost"));
	on_session_quit();
}

void Gobby::Window::on_obby_user_join(const obby::user& user)
{
	// Tell user join to components
	m_folder.obby_user_join(user);
	m_userlist.obby_user_join(user);
	m_documentlist.obby_user_join(user);
	m_chat.obby_user_join(user);
	m_statusbar.obby_user_join(user);
}

void Gobby::Window::on_obby_user_part(const obby::user& user)
{
	// Tell user part to components
	m_folder.obby_user_part(user);
	m_userlist.obby_user_part(user);
	m_documentlist.obby_user_part(user);
	m_chat.obby_user_part(user);
	m_statusbar.obby_user_part(user);
}

void Gobby::Window::on_obby_user_colour(const obby::user& user)
{
	m_userlist.obby_user_colour(user);
	m_documentlist.obby_user_colour(user);
	m_folder.obby_user_colour(user);
}

void Gobby::Window::on_obby_user_colour_failed()
{
	display_error(_("Colour change failed: Colour already in use") );
}

void Gobby::Window::on_obby_document_insert(obby::document_info& document)
{
	obby::local_document_info& local_doc =
		dynamic_cast<obby::local_document_info&>(document);

	m_folder.obby_document_insert(local_doc);
	m_userlist.obby_document_insert(local_doc);
	m_documentlist.obby_document_insert(local_doc);
	m_chat.obby_document_insert(local_doc);
	m_statusbar.obby_document_insert(local_doc);
}

void Gobby::Window::on_obby_document_remove(obby::document_info& document)
{
	obby::local_document_info& local_doc =
		dynamic_cast<obby::local_document_info&>(document);

	m_folder.obby_document_remove(local_doc);
	m_userlist.obby_document_remove(local_doc);
	m_documentlist.obby_document_remove(local_doc);
	m_chat.obby_document_remove(local_doc);
	m_statusbar.obby_document_remove(local_doc);
}

Gobby::Document* Gobby::Window::get_current_document()
{
	// No document open
	if(m_folder.get_n_pages() == 0) return NULL;

	// Get currently selected page
	Widget* page = m_folder.get_nth_page(m_folder.get_current_page() );
	// Convert to document
	return &static_cast<DocWindow*>(page)->get_document();
}

void Gobby::Window::apply_preferences()
{
	m_header.get_toolbar().set_toolbar_style(
		m_preferences.appearance.toolbar_show);
}

void Gobby::Window::update_title_bar()
{
	// No document
	if(m_folder.get_n_pages() == 0)
	{
		set_title("Gobby");
		return;
	}

	// Get currently active document
	const Document& document = *get_current_document();
	// Get title of current document
	const Glib::ustring& file = document.get_title();
	// Get path of current document
	Glib::ustring path = document.get_path();

	// Show path in title, if we know it
	if(!path.empty() )
	{
		// Replace home dir by ~
		Glib::ustring home = Glib::get_home_dir();
		if(path.compare(0, home.length(), home) == 0)
			path.replace(0, home.length(), "~");

		// Set title with file and path
		obby::format_string title_str("%0% (%1%) - Gobby");
		title_str << file << Glib::path_get_dirname(path);
		set_title(title_str.str() );
	}
	else
	{
		// Path not known: Set title with file only
		obby::format_string title_str("%0% - Gobby");
		title_str << file;
		set_title(title_str.str() );
	}
}

namespace
{
	// convert2unix converts a given string from any special line endings
	// (DOS or old-style Macintosh) to Unix line endings. It does no
	// book-keeping about the encountered endings but ensures that no
	// CR characters are left in the string.
	void convert2unix(std::string& str)
	{
		for(std::string::size_type i = 0; i < str.length(); ++ i)
			// Convert DOS CRLF to a single LF
			if(str[i] == '\r' && str[i+1] == '\n')
				str.erase(i, 1);
			// Convert Macintosh CR to LF
			else if(str[i] == '\r')
				str[i] = '\n';
	}
}

void Gobby::Window::open_local_file(const Glib::ustring& file)
{
	try
	{
		// Set local file path for the document_insert callback
		m_local_file_path = file;

		std::string content(
			convert_to_utf8(Glib::file_get_contents(file)) );
		convert2unix(content);

		m_buffer->document_create(
			Glib::path_get_basename(file), content
		);
	}
	catch(Glib::Exception& e)
	{
		// Show errors while opening the file (e.g. if it doesn't exist)
		display_error(e.what() );
	}
}

void Gobby::Window::save_local_file(Document& doc, const Glib::ustring& file)
{
	// Open stream to file
	std::ofstream stream(file.c_str() );

	try
	{
		if(!stream)
		{
			obby::format_string str(
				_("Could not open file '%0%' for writing")
			);
			str << file;
			throw std::runtime_error(str.str() );
		}

		// Save content into file
		stream << doc.get_content().raw();
		stream.close();

		// Set path of document
		doc.set_path(file);
		// Update title bar according to new path
		update_title_bar();
		// Unset modifified flag
		doc.get_buffer()->set_modified(false);
	}
	catch(std::exception& e)
	{
		display_error(e.what() );
	}
}

void Gobby::Window::close_document(DocWindow& document)
{
	// Check for the document being modified
	if(document.get_document().get_modified() )
	{
		// Setup confirmation strings
		obby::format_string primary_str(
			_("Save changes to document \"%0%\" before closing?")
		);

		obby::format_string secondary_str(
			_("If you don't save, changes will be discarded, but "
			  "may still be retrieved if you re-subscribe to the "
			  "document.")
		);

		primary_str << document.get_document().get_title();

		// Setup dialog
		Gtk::MessageDialog dlg(*this, primary_str.str(), false,
			Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);

		// Set secondary text
		dlg.set_secondary_text(secondary_str.str() );

		// Add button to allow the user to save the dialog
		dlg.add_button("Close without saving", Gtk::RESPONSE_REJECT);
		dlg.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
		dlg.add_button(Gtk::Stock::SAVE, Gtk::RESPONSE_ACCEPT)->
			grab_focus();

		// Show the dialog
		int result = dlg.run();

		switch(result)
		{
		case Gtk::RESPONSE_REJECT:
			/* Close the document */
			break;
		case Gtk::RESPONSE_ACCEPT:
			/* Save the document before closing it */
			m_folder.set_current_page(m_folder.page_num(document) );
			// TODO: Do not close the document if the user cancells
			// the save dialog.
			on_document_save();
			break;
		case Gtk::RESPONSE_CANCEL:
		case Gtk::RESPONSE_DELETE_EVENT:
			/* Do not close the document */
			return;
			break;
		default:
			throw std::logic_error("Gobby::Window::close_document");
			break;
		}
	}

	if(m_buffer.get() != NULL)
	{
		// Unsubscribe from document
		document.get_document().get_document().unsubscribe();
	}
	else
	{
		// Buffer does not exist: Maybe the connection has been lost
		// or something: Just remove the document from the folder.
		m_folder.remove_page(document);
		on_folder_document_remove(document);
	}
}

void Gobby::Window::display_error(const Glib::ustring& message,
                                  const Gtk::MessageType type)
{
	Gtk::MessageDialog dlg(*this, message, false, type,
	                       Gtk::BUTTONS_OK, true);
	dlg.run();
}
