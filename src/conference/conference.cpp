/*
 * conference.cpp
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

#include "call/call-listener.h"
#include "conference-p.h"
#include "conference/session/call-session-p.h"
#include "logger/logger.h"
#include "participant-p.h"

// =============================================================================

using namespace std;

LINPHONE_BEGIN_NAMESPACE

Conference::Conference (ConferencePrivate &p, LinphoneCore *core, const Address &myAddress, CallListener *listener) : mPrivate(&p) {
	L_D();
	d->mPublic = this;
	d->core = core;
	d->callListener = listener;
	d->me = ObjectFactory::create<Participant>(myAddress);
}

Conference::~Conference () {
	delete mPrivate;
}

// -----------------------------------------------------------------------------

shared_ptr<Participant> Conference::getActiveParticipant () const {
	L_D();
	return d->activeParticipant;
}

shared_ptr<Participant> Conference::getMe () const {
	L_D();
	return d->me;
}

LinphoneCore *Conference::getCore () const {
	L_D();
	return d->core;
}

// -----------------------------------------------------------------------------

void Conference::addParticipant (const Address &addr, const CallSessionParams *params, bool hasMedia) {
	lError() << "Conference class does not handle addParticipant() generically";
}

void Conference::addParticipants (const list<Address> &addresses, const CallSessionParams *params, bool hasMedia) {
	list<Address> sortedAddresses(addresses);
	sortedAddresses.sort();
	sortedAddresses.unique();
	for (const auto &addr: sortedAddresses) {
		shared_ptr<Participant> participant = findParticipant(addr);
		if (!participant)
			addParticipant(addr, params, hasMedia);
	}
}

bool Conference::canHandleParticipants () const {
	return true;
}

const Address &Conference::getConferenceAddress () const {
	L_D();
	return d->conferenceAddress;
}

int Conference::getNbParticipants () const {
	L_D();
	return static_cast<int>(d->participants.size());
}

list<shared_ptr<Participant>> Conference::getParticipants () const {
	L_D();
	return d->participants;
}

const string &Conference::getSubject () const {
	L_D();
	return d->subject;
}

void Conference::join () {}

void Conference::leave () {}

void Conference::removeParticipant (const shared_ptr<const Participant> &participant) {
	lError() << "Conference class does not handle removeParticipant() generically";
}

void Conference::removeParticipants (const list<shared_ptr<Participant>> &participants) {
	for (const auto &p : participants)
		removeParticipant(p);
}

void Conference::setParticipantAdminStatus (shared_ptr<Participant> &participant, bool isAdmin) {
	lError() << "Conference class does not handle setParticipantAdminStatus() generically";
}

void Conference::setSubject (const string &subject) {
	L_D();
	d->subject = subject;
}

// -----------------------------------------------------------------------------

void Conference::onAckBeingSent (const shared_ptr<const CallSession> &session, LinphoneHeaders *headers) {
	L_D();
	if (d->callListener)
		d->callListener->onAckBeingSent(headers);
}

void Conference::onAckReceived (const shared_ptr<const CallSession> &session, LinphoneHeaders *headers) {
	L_D();
	if (d->callListener)
		d->callListener->onAckReceived(headers);
}

void Conference::onCallSessionAccepted (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onIncomingCallToBeAdded();
}

void Conference::onCallSessionSetReleased (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onCallSetReleased();
}

void Conference::onCallSessionSetTerminated (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onCallSetTerminated();
}

void Conference::onCallSessionStateChanged (const shared_ptr<const CallSession> &session, LinphoneCallState state, const string &message) {
	L_D();
	if (d->callListener)
		d->callListener->onCallStateChanged(state, message);
}

void Conference::onCheckForAcceptation (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onCheckForAcceptation();
}

void Conference::onIncomingCallSessionStarted (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onIncomingCallStarted();
}

void Conference::onEncryptionChanged (const shared_ptr<const CallSession> &session, bool activated, const string &authToken) {
	L_D();
	if (d->callListener)
		d->callListener->onEncryptionChanged(activated, authToken);
}

void Conference::onStatsUpdated (const LinphoneCallStats *stats) {
	L_D();
	if (d->callListener)
		d->callListener->onStatsUpdated(stats);
}

void Conference::onResetCurrentSession (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onResetCurrentCall();
}

void Conference::onSetCurrentSession (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onSetCurrentCall();
}

void Conference::onFirstVideoFrameDecoded (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onFirstVideoFrameDecoded();
}

void Conference::onResetFirstVideoFrameDecoded (const shared_ptr<const CallSession> &session) {
	L_D();
	if (d->callListener)
		d->callListener->onResetFirstVideoFrameDecoded();
}

// -----------------------------------------------------------------------------

shared_ptr<Participant> Conference::findParticipant (const Address &addr) const {
	L_D();

	Address testedAddr = addr;
	testedAddr.setPort(0);
	for (const auto &participant : d->participants) {
		Address participantAddr = participant->getAddress();
		participantAddr.setPort(0);
		if (testedAddr.weakEqual(participantAddr))
			return participant;
	}

	return nullptr;
}

shared_ptr<Participant> Conference::findParticipant (const shared_ptr<const CallSession> &session) const {
	L_D();

	for (const auto &participant : d->participants) {
		if (participant->getPrivate()->getSession() == session)
			return participant;
	}

	return nullptr;
}

bool Conference::isMe (const Address &addr) const {
	L_D();
	Address cleanedMe = d->me->getAddress();
	cleanedMe.setPort(0);
	Address cleanedAddr = addr;
	cleanedAddr.setPort(0);
	return cleanedAddr == cleanedMe;
}

LINPHONE_END_NAMESPACE
