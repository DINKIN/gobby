/* gobby - A GTKmm driven libobby client
 * Copyright (C) 2005 - 2008 0x539 dev group
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

#include "commands/folder-commands.hpp"

class Gobby::FolderCommands::DocInfo: public sigc::trackable
{
public:
	static const unsigned int ACTIVATION_DELAY = 1000;

	DocInfo(DocWindow& document):
		m_document(document), m_active_user(NULL), m_active(false)
	{
		m_document.signal_active_user_changed().connect(
			sigc::mem_fun(
				*this, &DocInfo::on_active_user_changed));

		on_active_user_changed(document.get_active_user());
	}

	~DocInfo()
	{
		if(m_active_user != NULL)
		{
			g_signal_handler_disconnect(G_OBJECT(m_active_user),
			                            m_notify_status_handle);
		}
	}

	void deactivate()
	{
		m_active = false;
		if(m_active_user) deactivate_user();
	}

	void activate()
	{
		m_active = true;
		if(m_active_user) activate_user();
	}

protected:
	void activate_user()
	{
		g_assert(!m_timeout_connection.connected());
		g_assert(m_active_user != NULL);
		g_assert(inf_user_get_status(m_active_user) ==
		         INF_USER_INACTIVE);

		m_timeout_connection = Glib::signal_timeout().connect(
			sigc::mem_fun(*this, &DocInfo::on_activation_timeout),
			ACTIVATION_DELAY);
	}

	void deactivate_user()
	{
		g_assert(m_active_user != NULL);

		switch(inf_user_get_status(m_active_user))
		{
		case INF_USER_INACTIVE:
			g_assert(m_timeout_connection.connected());
			m_timeout_connection.disconnect();
			break;
		case INF_USER_ACTIVE:
			inf_session_set_user_status(
				INF_SESSION(m_document.get_session()),
				m_active_user, INF_USER_INACTIVE);
			break;
		case INF_USER_UNAVAILABLE:
			// It can happen that the user is already unavailable
			// here, for example when we have lost the connection
			// to the server, so this is not an error.

			// TODO: Shouldn't local users stay available on
			// connection loss? We probably need to fix this
			// in infinote.
			break;
		}
		
	}

	static void on_user_notify_status_static(InfUser* user,
	                                         GParamSpec* pspec,
	                                         gpointer user_data)
	{
		static_cast<DocInfo*>(user_data)->on_user_notify_status(user);
	}

	void on_active_user_changed(InfTextUser* user)
	{
		if(m_active_user != NULL)
		{
			if(m_active)
				deactivate_user();

			g_signal_handler_disconnect(G_OBJECT(m_active_user),
			                            m_notify_status_handle);
		}

		m_active_user = INF_USER(user);

		if(user != NULL)
		{
			InfUserStatus user_status =
				inf_user_get_status(INF_USER(user));
			g_assert(user_status != INF_USER_UNAVAILABLE);

			m_notify_status_handle = g_signal_connect(
				G_OBJECT(user),
				"notify::status",
				G_CALLBACK(&on_user_notify_status_static),
				this
			);

			if( (user_status == INF_USER_ACTIVE && !m_active))
			{
				deactivate_user();
			}
			else if(user_status == INF_USER_INACTIVE && m_active)
			{
				activate_user();
			}
		}
	}

	void on_user_notify_status(InfUser* user)
	{
		if(inf_user_get_status(user) == INF_USER_ACTIVE && m_active)
		{
			// The user did something (therefore becoming active),
			// so we do not need to explictely activate the user.
			g_assert(m_timeout_connection.connected());
			m_timeout_connection.disconnect();
		}
	}

	bool on_activation_timeout()
	{
		// The user activated this document, but did not something for
		// a while, so explicitely set the user active
		g_assert(m_active);
		g_assert(m_active_user != NULL);
		g_assert(inf_user_get_status(m_active_user) ==
		         INF_USER_INACTIVE);

		inf_session_set_user_status(
			INF_SESSION(m_document.get_session()), m_active_user,
			INF_USER_ACTIVE);

		return false;
	}

	DocWindow& m_document;
	InfUser* m_active_user;
	bool m_active;

	sigc::connection m_timeout_connection;
	gulong m_notify_status_handle;
};

Gobby::FolderCommands::FolderCommands(Folder& folder):
	m_folder(folder), m_current_document(NULL)
{
	m_folder.signal_document_added().connect(
		sigc::mem_fun(*this, &FolderCommands::on_document_added));
	m_folder.signal_document_removed().connect(
		sigc::mem_fun(*this, &FolderCommands::on_document_removed));
	m_folder.signal_document_changed().connect(
		sigc::mem_fun(*this, &FolderCommands::on_document_changed));

	for(unsigned int i = 0; i < m_folder.get_n_pages(); ++ i)
	{
		DocWindow* window = static_cast<DocWindow*>(
			m_folder.get_nth_page(i));
		g_assert(window != NULL);

		on_document_added(*window);
	}

	on_document_changed(m_folder.get_current_document());
}

Gobby::FolderCommands::~FolderCommands()
{
	for(DocumentMap::iterator iter = m_doc_map.begin();
	    iter != m_doc_map.end(); ++ iter)
	{
		delete iter->second;
	}
}

void Gobby::FolderCommands::on_document_added(DocWindow& document)
{
	DocInfo* info = new DocInfo(document);
	m_doc_map[&document] = info;
}

void Gobby::FolderCommands::on_document_removed(DocWindow& document)
{
	DocumentMap::iterator iter = m_doc_map.find(&document);
	g_assert(iter != m_doc_map.end());

	delete iter->second;
	m_doc_map.erase(iter);

	// on_document_changed is called when another document has
	// been selected
	if(&document == m_current_document)
		m_current_document = NULL;
}

void Gobby::FolderCommands::on_document_changed(DocWindow* document)
{
	if(m_current_document != NULL)
	{
		DocumentMap::iterator iter =
			m_doc_map.find(m_current_document);
		g_assert(iter != m_doc_map.end());

		iter->second->deactivate();
	}

	m_current_document = document;

	if(document != NULL)
	{
		DocumentMap::iterator iter = m_doc_map.find(document);
		g_assert(iter != m_doc_map.end());

		iter->second->activate();
	}
}
