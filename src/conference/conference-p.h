/*
 * conference-p.h
 * Copyright (C) 2010-2017 Belledonne Communications SARL
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _CONFERENCE_P_H_
#define _CONFERENCE_P_H_

#include <list>
#include <memory>

#include "linphone/types.h"

#include "address/address.h"
#include "conference.h"

// =============================================================================

LINPHONE_BEGIN_NAMESPACE

class CallListener;
class Participant;

class ConferencePrivate {
public:
	Address conferenceAddress;
	std::list<std::shared_ptr<Participant>> participants;

protected:
	LinphoneCore *core = nullptr;
	CallListener *callListener = nullptr;

	std::shared_ptr<Participant> activeParticipant;
	std::shared_ptr<Participant> me;
	std::string subject;

	Conference *mPublic = nullptr;

private:
	L_DECLARE_PUBLIC(Conference);
};

LINPHONE_END_NAMESPACE

#endif // ifndef _CONFERENCE_P_H_
