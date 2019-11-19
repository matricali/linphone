/*
 * Copyright (c) 2010-2019 Belledonne Communications SARL.
 *
 * This file is part of Liblinphone.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <bctoolbox/defs.h>

#include "streams.h"
#include "media-session.h"
#include "media-session-p.h"
#include "core/core.h"
#include "c-wrapper/c-wrapper.h"
#include "call/call.h"
#include "call/call-p.h"
#include "conference/participant.h"
#include "utils/payload-type-handler.h"
#include "conference/params/media-session-params-p.h"

#include "linphone/core.h"


using namespace::std;

LINPHONE_BEGIN_NAMESPACE



void OfferAnswerContext::scopeStreamToIndex(size_t index){
	streamIndex = index;
	localStreamDescription = localMediaDescription ? &localMediaDescription->streams[index] : nullptr;
	remoteStreamDescription = remoteMediaDescription ? &remoteMediaDescription->streams[index] : nullptr;
	resultStreamDescription = resultMediaDescription ? &resultMediaDescription->streams[index] : nullptr;
}

/*
 * StreamGroup implementation
 */

StreamsGroup::StreamsGroup(MediaSession &session) : mMediaSession(session){
	mIceAgent.reset(new IceAgent(session));
}

IceAgent & StreamsGroup::getIceAgent()const{
	return *mIceAgent;
}

Stream * StreamsGroup::createStream(const OfferAnswerContext &params){
	Stream *ret = nullptr;
	SalStreamType type = params.localStreamDescription->type;
	switch(type){
		case SalAudio:
			ret = new MS2AudioStream(*this, params);
		break;
		case SalVideo:
			ret = new MS2VideoStream(*this, params);
		break;
		case SalText:
			ret = new MS2RTTStream(*this, params);
		break;
		case SalOther:
		break;
	}
	if (!ret){
		lError() << "Could not create Stream of type " << sal_stream_type_to_string(type);
		return nullptr;
	}
	
	if ((decltype(mStreams)::size_type)params.streamIndex >= mStreams.size()) mStreams.resize(params.streamIndex + 1);
	mStreams[params.streamIndex].reset(ret);
	return ret;
}

void StreamsGroup::createStreams(const OfferAnswerContext &params){
}

void StreamsGroup::prepare(){
	for (auto &stream : mStreams){
		if (stream->getState() == Stream::Stopped){
			stream->prepare();
		}
	}
}

void StreamsGroup::render(const OfferAnswerContext &params, CallSession::State targetState){
	OfferAnswerContext context = params;
	for(auto &stream : mStreams){
		context.scopeStreamToIndex(stream->getIndex());
		stream->render(context, targetState);
	}
}

void StreamsGroup::stop(){
	for(auto &stream : mStreams){
		if (stream && stream->getState() != Stream::Stopped)
			stream->stop();
	}
}

Stream * StreamsGroup::getStream(size_t index){
	if (index >=  mStreams.size()){
		lError() << "Bad stream index " << index;
		return nullptr;
	}
	return mStreams[index].get();
}

std::list<Stream*> StreamsGroup::getStreams(){
	list<Stream*> ret;
	for (auto &s : mStreams){
		if (s) ret.push_back(s.get());
	}
	return ret;
}

bool StreamsGroup::isPortUsed(int port)const{
	if (port == -1) return false;
	for(auto &stream : mStreams){
		if (stream && stream->isPortUsed(port)) return true;
	}
	return false;
}

LinphoneCore *StreamsGroup::getCCore()const{
	return mMediaSession.getCore()->getCCore();
}

MediaSessionPrivate &StreamsGroup::getMediaSessionPrivate()const{
	return *getMediaSession().getPrivate();
}

int StreamsGroup::updateAllocatedAudioBandwidth (const PayloadType *pt, int maxbw) {
	mAudioBandwidth = PayloadTypeHandler::getAudioPayloadTypeBandwidth(pt, maxbw);
	lInfo() << "Audio bandwidth for StreamsGroup [" << this << "] is " << mAudioBandwidth;
	return mAudioBandwidth;
}

int StreamsGroup::getVideoBandwidth (const SalMediaDescription *md, const SalStreamDescription *desc) {
	int remoteBandwidth = 0;
	if (desc->bandwidth > 0)
		remoteBandwidth = desc->bandwidth;
	else if (md->bandwidth > 0) {
		/* Case where b=AS is given globally, not per stream */
		remoteBandwidth = PayloadTypeHandler::getRemainingBandwidthForVideo(md->bandwidth, mAudioBandwidth);
	}
	return PayloadTypeHandler::getMinBandwidth(
		PayloadTypeHandler::getRemainingBandwidthForVideo(linphone_core_get_upload_bandwidth(getCCore()), mAudioBandwidth), remoteBandwidth);
}


void StreamsGroup::zrtpStarted(Stream *mainZrtpStream){
	for (auto &stream : mStreams){
		if (stream && stream.get() != mainZrtpStream) stream->zrtpStarted(mainZrtpStream);
	}
	propagateEncryptionChanged();
}

bool StreamsGroup::allStreamsEncrypted () const {
	int activeStreamsCount = 0;
	for (auto &stream : mStreams){
		if (stream->getState() == Stream::Running){
			++activeStreamsCount;
			if (!stream->isEncrypted()){
				return false;
			}
		}
	}
	return activeStreamsCount > 0;
}


void StreamsGroup::propagateEncryptionChanged () {
	getMediaSessionPrivate().propagateEncryptionChanged();
}

void StreamsGroup::authTokenReady(const string &authToken, bool verified) {
	mAuthToken = authToken;
	mAuthTokenVerified = verified;
	lInfo() << "Authentication token is " << mAuthToken << "(" << (mAuthTokenVerified ? "verified" : "unverified") << ")";
}

Stream * StreamsGroup::lookupMainStream(SalStreamType type){
	for (auto &stream : mStreams){
		if (stream->isMain() && stream->getType() == type){
			return stream.get();
		}
	}
	return nullptr;
}


/*
 * Stream implementation.
 */


Stream::Stream(StreamsGroup &sg, const OfferAnswerContext &params) : mStreamsGroup(sg), mStreamType(params.localStreamDescription->type), mIndex(params.streamIndex){
	setPortConfig();
	fillMulticastMediaAddresses();
}

LinphoneCore *Stream::getCCore()const{
	return getCore().getCCore();
}

Core &Stream::getCore()const{
	return *mStreamsGroup.getMediaSession().getCore();
}

MediaSession &Stream::getMediaSession()const{
	return mStreamsGroup.getMediaSession();
}

MediaSessionPrivate &Stream::getMediaSessionPrivate()const{
	return *getMediaSession().getPrivate();
}

void Stream::prepare(){
	mState = Preparing;
}

void Stream::render(const OfferAnswerContext & ctx, CallSession::State targetState){
	mState = Running;
}

void Stream::stop(){
	mState = Stopped;
}

void Stream::setRandomPortConfig () {
	mPortConfig.rtpPort = -1;
	mPortConfig.rtcpPort = -1;
}

int Stream::selectRandomPort (pair<int, int> portRange) {
	unsigned int rangeSize = static_cast<unsigned int>(portRange.second - portRange.first);
	
	for (int nbTries = 0; nbTries < 100; nbTries++) {
		bool alreadyUsed = false;
		unsigned int randomInRangeSize = (bctbx_random() % rangeSize) & (unsigned int)~0x1; /* Select an even number */
		int triedPort = ((int)randomInRangeSize) + portRange.first;
		/*If portRange.first is even, the triedPort will be even too. The one who configures a port range that starts with an odd number will
		 * get odd RTP port numbers.*/
		
		for (const bctbx_list_t *elem = linphone_core_get_calls(getCCore()); elem != nullptr; elem = bctbx_list_next(elem)) {
			LinphoneCall *lcall = reinterpret_cast<LinphoneCall *>(bctbx_list_get_data(elem));
			shared_ptr<MediaSession> session = static_pointer_cast<MediaSession>(L_GET_CPP_PTR_FROM_C_OBJECT(lcall)->getPrivate()->getActiveSession());
			if (session->getPrivate()->getStreamsGroup().isPortUsed(triedPort)) {
				alreadyUsed = true;
				break;
			}
		}
		if (!alreadyUsed){
			lInfo() << "Port " << triedPort << " randomly taken from range [ " << portRange.first << " , " << portRange.second << "]";
			return triedPort;
		}
	}

	lError() << "Could not find any free port!";
	return -1;
}

int Stream::selectFixedPort (pair<int, int> portRange) {
	for (int triedPort = portRange.first; triedPort < (portRange.first + 100); triedPort += 2) {
		bool alreadyUsed = false;
		for (const bctbx_list_t *elem = linphone_core_get_calls(getCCore()); elem != nullptr; elem = bctbx_list_next(elem)) {
			LinphoneCall *lcall = reinterpret_cast<LinphoneCall *>(bctbx_list_get_data(elem));
			shared_ptr<MediaSession> session = static_pointer_cast<MediaSession>(L_GET_CPP_PTR_FROM_C_OBJECT(lcall)->getPrivate()->getActiveSession());
			if (session->getPrivate()->getStreamsGroup().isPortUsed(triedPort)) {
				alreadyUsed = true;
				break;
			}
		}
		if (!alreadyUsed)
			return triedPort;
	}

	lError() << "Could not find any free port !";
	return -1;
}

void Stream::setPortConfig(pair<int, int> portRange) {
	if ((portRange.first <= 0) && (portRange.second <= 0)) {
		setRandomPortConfig();
	} else {
		if (portRange.first == portRange.second) {
			/* Fixed port */
			mPortConfig.rtpPort = selectFixedPort(portRange);
		} else {
			/* Select random port in the specified range */
			mPortConfig.rtpPort = selectRandomPort(portRange);
		}
	}
	if (mPortConfig.rtpPort == -1) setRandomPortConfig();
	else mPortConfig.rtcpPort = mPortConfig.rtpPort + 1;
}

void Stream::setPortConfig(){
	int minPort = 0, maxPort = 0;
	switch(getType()){
		case SalAudio:
			linphone_core_get_audio_port_range(getCCore(), &minPort, &maxPort);
		break;
		case SalVideo:
			linphone_core_get_video_port_range(getCCore(), &minPort, &maxPort);
		break;
		case SalText:
			linphone_core_get_text_port_range(getCCore(), &minPort, &maxPort);
		break;
		case SalOther:
		break;
	}
	setPortConfig(make_pair(minPort, maxPort));
}

void Stream::fillMulticastMediaAddresses () {
	mPortConfig.multicastIp.clear();
	if (getType() == SalAudio && getMediaSession().getPrivate()->getParams()->audioMulticastEnabled()){
		mPortConfig.multicastIp = linphone_core_get_audio_multicast_addr(getCCore());
	} else if (getType() == SalVideo && getMediaSession().getPrivate()->getParams()->videoMulticastEnabled()){
		mPortConfig.multicastIp = linphone_core_get_video_multicast_addr(getCCore());
	}
}

bool Stream::isPortUsed(int port)const{
	return port == mPortConfig.rtpPort || port == mPortConfig.rtcpPort;
}

IceAgent & Stream::getIceAgent()const{
	return mStreamsGroup.getIceAgent();
}


/*
 * MS2Stream implementation
 */

MS2Stream::MS2Stream(StreamsGroup &sg, const OfferAnswerContext &params) : Stream(sg, params){
	memset(&mSessions, 0, sizeof(mSessions));
	initMulticast(params);
	
}

string MS2Stream::getBindIp(){
	string bindIp = lp_config_get_string(linphone_core_get_config(getCCore()), "rtp", "bind_address", "");
	
	if (!mPortConfig.multicastIp.empty()){
		if (mPortConfig.multicastRole == SalMulticastSender) {
			/* As multicast sender, we must decide a local interface to use to send multicast, and bind to it */
			char multicastBindIp[LINPHONE_IPADDR_SIZE] = {0};
			linphone_core_get_local_ip_for((mPortConfig.multicastIp.find_first_of(':') == string::npos) ? AF_INET : AF_INET6, nullptr, multicastBindIp);
			bindIp = mPortConfig.multicastBindIp = multicastBindIp;
		} else {
			/* Otherwise we shall use an address family of the same family of the multicast address, because
			 * dual stack socket and multicast don't work well on Mac OS (linux is OK, as usual). */
			bindIp = (mPortConfig.multicastIp.find_first_of(':') == string::npos) ? "0.0.0.0" : "::0";
		}
	}else if (bindIp.empty()){
		/*If ipv6 is not enabled, listen to 0.0.0.0. The default behavior of mediastreamer when no IP is passed is to try ::0, and in
		 * case of failure try 0.0.0.0 . But we don't want this if IPv6 is explicitely disabled.*/
		if (!linphone_core_ipv6_enabled(getCCore())){
			bindIp = "0.0.0.0";
		}
	}
	return bindIp;
}

void MS2Stream::initMulticast(const OfferAnswerContext &params) {
	if (params.localIsOfferer) {
		mPortConfig.multicastRole = params.localStreamDescription->multicast_role;
	}else{
		mPortConfig.multicastRole = params.remoteStreamDescription->multicast_role;
	}
	if (mPortConfig.multicastRole == SalMulticastReceiver){
		mPortConfig.rtpPort = params.remoteStreamDescription->rtp_port;
		mPortConfig.rtpPort = 0; /*RTCP deactivated in multicast*/
	}
	lInfo() << this << "multicast role is ["
		<< sal_multicast_role_to_string(mPortConfig.multicastRole) << "]";
}

void MS2Stream::configureRtpSessionForRtcpFb (const OfferAnswerContext &params) {
	if (getType() != SalAudio && getType() != SalVideo) return; //No AVPF for other than audio/video
	
	rtp_session_enable_avpf_feature(mSessions.rtp_session, ORTP_AVPF_FEATURE_GENERIC_NACK, !!params.resultStreamDescription->rtcp_fb.generic_nack_enabled);
	rtp_session_enable_avpf_feature(mSessions.rtp_session, ORTP_AVPF_FEATURE_TMMBR, !!params.resultStreamDescription->rtcp_fb.tmmbr_enabled);
}

void MS2Stream::configureRtpSessionForRtcpXr(const OfferAnswerContext &params) {
	OrtpRtcpXrConfiguration currentConfig;
	const OrtpRtcpXrConfiguration *remoteConfig = &params.remoteStreamDescription->rtcp_xr;
	if (params.localStreamDescription->dir == SalStreamInactive)
		return;
	else if (params.localStreamDescription->dir == SalStreamRecvOnly) {
		/* Use local config for unilateral parameters and remote config for collaborative parameters */
		memcpy(&currentConfig, &params.localStreamDescription->rtcp_xr, sizeof(currentConfig));
		currentConfig.rcvr_rtt_mode = remoteConfig->rcvr_rtt_mode;
		currentConfig.rcvr_rtt_max_size = remoteConfig->rcvr_rtt_max_size;
	} else
		memcpy(&currentConfig, remoteConfig, sizeof(currentConfig));
	
	rtp_session_configure_rtcp_xr(mSessions.rtp_session, &currentConfig);
}

void MS2Stream::configureAdaptiveRateControl (const OfferAnswerContext &params) {
	bool videoWillBeUsed = false;
	MediaStream *ms = getMediaStream();

	const SalStreamDescription *vstream = sal_media_description_find_best_stream(const_cast<SalMediaDescription*>(params.resultMediaDescription), SalVideo);
	if (vstream && (vstream->dir != SalStreamInactive) && vstream->payloads) {
		/* When video is used, do not make adaptive rate control on audio, it is stupid */
		videoWillBeUsed = true;
	}
	bool enabled = !!linphone_core_adaptive_rate_control_enabled(getCCore());
	if (!enabled) {
		media_stream_enable_adaptive_bitrate_control(ms, false);
		return;
	}
	bool isAdvanced = true;
	string algo = linphone_core_get_adaptive_rate_algorithm(getCCore());
	if (algo == "basic")
		isAdvanced = false;
	else if (algo == "advanced")
		isAdvanced = true;
	
	if (isAdvanced && !params.resultStreamDescription->rtcp_fb.tmmbr_enabled) {
		lWarning() << "Advanced adaptive rate control requested but avpf-tmmbr is not activated in this stream. Reverting to basic rate control instead";
		isAdvanced = false;
	}
	if (isAdvanced) {
		lInfo() << "Setting up advanced rate control";
		ms_bandwidth_controller_add_stream(getCCore()->bw_controller, ms);
		media_stream_enable_adaptive_bitrate_control(ms, false);
	} else {
		media_stream_set_adaptive_bitrate_algorithm(ms, MSQosAnalyzerAlgorithmSimple);
		if (getType() == SalAudio && videoWillBeUsed) {
			/* If this is an audio stream but video is going to be used, there is no need to perform
			 * basic rate control on the audio stream, just the video stream. */
			enabled = false;
		}
		media_stream_enable_adaptive_bitrate_control(ms, enabled);
	}
}


void MS2Stream::render(const OfferAnswerContext &params, CallSession::State targetState){
	const SalStreamDescription *stream = params.resultStreamDescription;
	const char *rtpAddr = (stream->rtp_addr[0] != '\0') ? stream->rtp_addr : params.resultMediaDescription->addr;
	bool isMulticast = !!ms_is_multicast(rtpAddr);
	
	
	media_stream_set_max_network_bitrate(getMediaStream(), linphone_core_get_upload_bandwidth(getCCore()) * 1000);
	if (isMulticast)
		rtp_session_set_multicast_ttl(mSessions.rtp_session, stream->ttl);
	rtp_session_enable_rtcp_mux(mSessions.rtp_session, stream->rtcp_mux);
		// Valid local tags are > 0
	if (sal_stream_description_has_srtp(stream)) {
		int cryptoIdx = Sal::findCryptoIndexFromTag(params.localStreamDescription->crypto, static_cast<unsigned char>(stream->crypto_local_tag));
		if (cryptoIdx >= 0) {
			ms_media_stream_sessions_set_srtp_recv_key_b64(&mSessions, stream->crypto[0].algo, stream->crypto[0].master_key);
			ms_media_stream_sessions_set_srtp_send_key_b64(&mSessions, stream->crypto[0].algo, 
								       params.localStreamDescription->crypto[cryptoIdx].master_key);
		} else
			lWarning() << "Failed to find local crypto algo with tag: " << stream->crypto_local_tag;
	}
	ms_media_stream_sessions_set_encryption_mandatory(&mSessions, getMediaSessionPrivate().isEncryptionMandatory());
	configureRtpSessionForRtcpFb(params);
	configureRtpSessionForRtcpXr(params);
	configureAdaptiveRateControl(params);
	
	Stream::render(params, targetState);
}


OrtpJitterBufferAlgorithm MS2Stream::jitterBufferNameToAlgo (const string &name) {
	if (name == "basic") return OrtpJitterBufferBasic;
	if (name == "rls") return OrtpJitterBufferRecursiveLeastSquare;
	lError() << "Invalid jitter buffer algorithm: " << name;
	return OrtpJitterBufferRecursiveLeastSquare;
}

void MS2Stream::applyJitterBufferParams (RtpSession *session) {
	LinphoneConfig *config = linphone_core_get_config(getCCore());
	JBParameters params;
	rtp_session_get_jitter_buffer_params(session, &params);
	params.min_size = lp_config_get_int(config, "rtp", "jitter_buffer_min_size", 40);
	params.max_size = lp_config_get_int(config, "rtp", "jitter_buffer_max_size", 500);
	params.max_packets = params.max_size * 200 / 1000; /* Allow 200 packet per seconds, quite large */
	const char *algo = lp_config_get_string(config, "rtp", "jitter_buffer_algorithm", "rls");
	params.buffer_algorithm = jitterBufferNameToAlgo(algo ? algo : "");
	params.refresh_ms = lp_config_get_int(config, "rtp", "jitter_buffer_refresh_period", 5000);
	params.ramp_refresh_ms = lp_config_get_int(config, "rtp", "jitter_buffer_ramp_refresh_period", 5000);
	params.ramp_step_ms = lp_config_get_int(config, "rtp", "jitter_buffer_ramp_step", 20);
	params.ramp_threshold = lp_config_get_int(config, "rtp", "jitter_buffer_ramp_threshold", 70);

	switch (getType()) {
		case SalAudio:
		case SalText: /* Let's use the same params for text as for audio */
			params.nom_size = linphone_core_get_audio_jittcomp(getCCore());
			params.adaptive = linphone_core_audio_adaptive_jittcomp_enabled(getCCore());
			break;
		case SalVideo:
			params.nom_size = linphone_core_get_video_jittcomp(getCCore());
			params.adaptive = linphone_core_video_adaptive_jittcomp_enabled(getCCore());
			break;
		default:
			lError() << "applyJitterBufferParams(): should not happen";
			break;
	}
	params.enabled = params.nom_size > 0;
	if (params.enabled) {
		if (params.min_size > params.nom_size)
			params.min_size = params.nom_size;
		if (params.max_size < params.nom_size)
			params.max_size = params.nom_size;
	}
	rtp_session_set_jitter_buffer_params(session, &params);
}

void MS2Stream::configureRtpSession(RtpSession *session){
	rtp_session_enable_network_simulation(session, &getCCore()->net_conf.netsim_params);
	applyJitterBufferParams(session);
	string userAgent = linphone_core_get_user_agent(getCCore());
	rtp_session_set_source_description(session, getMediaSessionPrivate().getMe()->getAddress().asString().c_str(), NULL, NULL, NULL, NULL, userAgent.c_str(), NULL);
	rtp_session_set_symmetric_rtp(session, linphone_core_symmetric_rtp_enabled(getCCore()));
	
	if (getType() == SalVideo){
		int videoRecvBufSize = lp_config_get_int(linphone_core_get_config(getCCore()), "video", "recv_buf_size", 0);
		if (videoRecvBufSize > 0)
			rtp_session_set_recv_buf_size(session, videoRecvBufSize);
	}
}

void MS2Stream::setupDtlsParams (MediaStream *ms) {
	if (getMediaSessionPrivate().getParams()->getMediaEncryption() == LinphoneMediaEncryptionDTLS) {
		MSDtlsSrtpParams dtlsParams = { 0 };
		
		/* TODO : search for a certificate with CNAME=sip uri(retrieved from variable me) or default : linphone-dtls-default-identity */
		/* This will parse the directory to find a matching fingerprint or generate it if not found */
		/* returned string must be freed */
		char *certificate = nullptr;
		char *key = nullptr;
		char *fingerprint = nullptr;

		sal_certificates_chain_parse_directory(&certificate, &key, &fingerprint,
			linphone_core_get_user_certificates_path(getCCore()), "linphone-dtls-default-identity", SAL_CERTIFICATE_RAW_FORMAT_PEM, true, true);
		if (fingerprint) {
			if (getMediaSessionPrivate().getDtlsFingerprint().empty()){
				getMediaSessionPrivate().setDtlsFingerprint(fingerprint);
			}
			ms_free(fingerprint);
		}
		if (key && certificate) {
			dtlsParams.pem_certificate = certificate;
			dtlsParams.pem_pkey = key;
			dtlsParams.role = MSDtlsSrtpRoleUnset; /* Default is unset, then check if we have a result SalMediaDescription */
			media_stream_enable_dtls(ms, &dtlsParams);
			ms_free(certificate);
			ms_free(key);
		} else {
			lError() << "Unable to retrieve or generate DTLS certificate and key - DTLS disabled";
			/* TODO : check if encryption forced, if yes, stop call */
		}
	}
}

void MS2Stream::initializeSessions(MediaStream *stream){
	if (mPortConfig.multicastRole == SalMulticastReceiver){
		if (!mPortConfig.multicastIp.empty())
			media_stream_join_multicast_group(stream, mPortConfig.multicastIp.c_str());
		else
			lError() << "Cannot join multicast group if multicast ip is not set";
	}
	
	configureRtpSession(stream->sessions.rtp_session);
	setupDtlsParams(stream);
	
	if (mPortConfig.rtpPort == -1){
		// Case where we requested random ports from the system. Now that they are allocated, get them.
		mPortConfig.rtpPort = rtp_session_get_local_port(stream->sessions.rtp_session);
		mPortConfig.rtcpPort = rtp_session_get_local_rtcp_port(stream->sessions.rtp_session);
	}
	int dscp = -1;
	switch(getType()){
		case SalAudio:
			dscp = linphone_core_get_audio_dscp(getCCore());
		break;
		case SalVideo:
			dscp = linphone_core_get_video_dscp(getCCore());
		break;
		default:
		break;
		
	}
	if (dscp != -1)
		media_stream_set_dscp(stream, dscp);
	
	mOrtpEvQueue = ortp_ev_queue_new();
	rtp_session_register_event_queue(stream->sessions.rtp_session, mOrtpEvQueue);
	
	media_stream_reclaim_sessions(stream, &mSessions);
	
}

void MS2Stream::prepare(){
	if (getCCore()->rtptf) {
		RtpTransport *meta_rtp;
		RtpTransport *meta_rtcp;
		rtp_session_get_transports(mSessions.rtp_session, &meta_rtp, &meta_rtcp);
		LinphoneCoreRtpTransportFactoryFunc rtpFunc = nullptr, rtcpFunc = nullptr;
		void *rtpFuncData = nullptr, *rtcpFuncData = nullptr;
		
		switch(getType()){
			case SalAudio:
				rtpFunc = getCCore()->rtptf->audio_rtp_func;
				rtpFuncData = getCCore()->rtptf->audio_rtp_func_data;
				rtcpFunc = getCCore()->rtptf->audio_rtcp_func;
				rtcpFuncData = getCCore()->rtptf->audio_rtcp_func_data;
			break;
			case SalVideo:
				rtpFunc = getCCore()->rtptf->video_rtp_func;
				rtpFuncData = getCCore()->rtptf->video_rtp_func_data;
				rtcpFunc = getCCore()->rtptf->video_rtcp_func;
				rtcpFuncData = getCCore()->rtptf->video_rtcp_func_data;
			break;
			case SalText:
			break;
			case SalOther:
			break;
		}
		
		if (!meta_rtp_transport_get_endpoint(meta_rtp)) {
			lInfo() << this << " using custom RTP transport endpoint";
			meta_rtp_transport_set_endpoint(meta_rtp, rtpFunc(rtpFuncData, mPortConfig.rtpPort));
		}
		if (!meta_rtp_transport_get_endpoint(meta_rtcp))
			meta_rtp_transport_set_endpoint(meta_rtcp, rtcpFunc(rtcpFuncData, mPortConfig.rtcpPort));
	}
	getIceAgent().prepareIceForStream(getMediaStream(), false);
	mTimer = getCore().createTimer([this](){
			handleEvents();
			return true;
		}, sEventPollIntervalMs, "Stream event processing timer");
	Stream::prepare();
}

int MS2Stream::getIdealAudioBandwidth (const SalMediaDescription *md, const SalStreamDescription *desc) {
	int remoteBandwidth = 0;
	if (desc->bandwidth > 0)
		remoteBandwidth = desc->bandwidth;
	else if (md->bandwidth > 0) {
		/* Case where b=AS is given globally, not per stream */
		remoteBandwidth = md->bandwidth;
	}
	int uploadBandwidth = 0;
	bool forced = false;
	if (getMediaSessionPrivate().getParams()->getPrivate()->getUpBandwidth() > 0) {
		forced = true;
		uploadBandwidth = getMediaSessionPrivate().getParams()->getPrivate()->getUpBandwidth();
	} else
		uploadBandwidth = linphone_core_get_upload_bandwidth(getCCore());
	uploadBandwidth = PayloadTypeHandler::getMinBandwidth(uploadBandwidth, remoteBandwidth);
	if (!linphone_core_media_description_contains_video_stream(md) || forced)
		return uploadBandwidth;
	
	/*
	 * This a default heuristic to choose a target upload bandwidth for an audio stream, the
	 * remaining can then be allocated for video.
	 */
	if (PayloadTypeHandler::bandwidthIsGreater(uploadBandwidth, 512))
		uploadBandwidth = 100;
	else if (PayloadTypeHandler::bandwidthIsGreater(uploadBandwidth, 256))
		uploadBandwidth = 64;
	else if (PayloadTypeHandler::bandwidthIsGreater(uploadBandwidth, 128))
		uploadBandwidth = 40;
	else if (PayloadTypeHandler::bandwidthIsGreater(uploadBandwidth, 0))
		uploadBandwidth = 24;
	return uploadBandwidth;
}

RtpProfile * MS2Stream::makeProfile(const SalMediaDescription *md, const SalStreamDescription *desc, int *usedPt) {
	if (mRtpProfile){
		rtp_profile_destroy(mRtpProfile);
		mRtpProfile = nullptr;
	}
	*usedPt = -1;
	int bandwidth = 0;
	if (desc->type == SalAudio)
		bandwidth = getIdealAudioBandwidth(md, desc);
	else if (desc->type == SalVideo)
		bandwidth = getGroup().getVideoBandwidth(md, desc);

	bool first = true;
	RtpProfile *profile = rtp_profile_new("Call profile");
	for (const bctbx_list_t *elem = desc->payloads; elem != nullptr; elem = bctbx_list_next(elem)) {
		OrtpPayloadType *pt = reinterpret_cast<OrtpPayloadType *>(bctbx_list_get_data(elem));
		/* Make a copy of the payload type, so that we left the ones from the SalStreamDescription unchanged.
		 * If the SalStreamDescription is freed, this will have no impact on the running streams. */
		pt = payload_type_clone(pt);
		int upPtime = 0;
		if ((pt->flags & PAYLOAD_TYPE_FLAG_CAN_SEND) && first) {
			/* First codec in list is the selected one */
			if (desc->type == SalAudio) {
				bandwidth = getGroup().updateAllocatedAudioBandwidth(pt, bandwidth);
				upPtime = getMediaSessionPrivate().getParams()->getPrivate()->getUpPtime();
				if (!upPtime)
					upPtime = linphone_core_get_upload_ptime(getCCore());
			}
			first = false;
		}
		if (*usedPt == -1) {
			/* Don't select telephone-event as a payload type */
			if (strcasecmp(pt->mime_type, "telephone-event") != 0)
				*usedPt = payload_type_get_number(pt);
		}
		if (pt->flags & PAYLOAD_TYPE_BITRATE_OVERRIDE) {
			lInfo() << "Payload type [" << pt->mime_type << "/" << pt->clock_rate << "] has explicit bitrate [" << (pt->normal_bitrate / 1000) << "] kbit/s";
			pt->normal_bitrate = PayloadTypeHandler::getMinBandwidth(pt->normal_bitrate, bandwidth * 1000);
		} else
			pt->normal_bitrate = bandwidth * 1000;
		if (desc->maxptime > 0) {// follow the same schema for maxptime as for ptime. (I.E add it to fmtp)
			ostringstream os;
			os << "maxptime=" << desc->maxptime;
			payload_type_append_send_fmtp(pt, os.str().c_str());
		}
		if (desc->ptime > 0)
			upPtime = desc->ptime;
		if (upPtime > 0) {
			ostringstream os;
			os << "ptime=" << upPtime;
			payload_type_append_send_fmtp(pt, os.str().c_str());
		}
		int number = payload_type_get_number(pt);
		if (rtp_profile_get_payload(profile, number))
			lWarning() << "A payload type with number " << number << " already exists in profile!";
		else
			rtp_profile_set_payload(profile, number, pt);
	}
	mRtpProfile = profile;
	return profile;
}


void MS2Stream::updateStats(){
	if (mSessions.rtp_session) {
		const rtp_stats_t *rtpStats = rtp_session_get_stats(mSessions.rtp_session);
		if (rtpStats)
			_linphone_call_stats_set_rtp_stats(mStats, rtpStats);
	}
	float quality = media_stream_get_average_quality_rating(getMediaStream());
	LinphoneCallLog *log = getMediaSession().getLog();
	if (quality >= 0) {
		if (log->quality == -1.0)
			log->quality = quality;
		else
			log->quality *= quality / 5.0f;
	}
}

void MS2Stream::stop(){
	CallSessionListener *listener = getMediaSessionPrivate().getCallSessionListener();
	
	if (listener){
		int statsType = -1;
		switch(getType()){
			case SalAudio: statsType = LINPHONE_CALL_STATS_AUDIO; break;
			case SalVideo: statsType = LINPHONE_CALL_STATS_VIDEO; break;
			case SalText: statsType = LINPHONE_CALL_STATS_TEXT; break;
			default:
				break;
			
		}
		
		if (statsType != -1) listener->onUpdateMediaInfoForReporting(getMediaSession().getSharedFromThis(), LINPHONE_CALL_STATS_AUDIO);
		
		/*
		 * FIXME : very very ugly way to manage the conference. Worse, it can remove from a conference a stream that has never been part 
		 * of any conference.
		 * Solution: let the Conference object manage the StreamsGroups that are part of a conference.
		 */
		if (getType() == SalAudio) listener->onCallSessionConferenceStreamStopping(getMediaSession().getSharedFromThis());
	}
	ms_bandwidth_controller_remove_stream(getCCore()->bw_controller, getMediaStream());
	
	if (mRtpProfile){
		rtp_profile_destroy(mRtpProfile);
		mRtpProfile = nullptr;
	}
	
	if (mRtpIoProfile){
		rtp_profile_destroy(mRtpIoProfile);
		mRtpIoProfile = nullptr;
	}
	
	updateStats();
	handleEvents();
	if (mTimer){
		getCore().destroyTimer(mTimer);
		mTimer = nullptr;
	}
	Stream::stop();
}

void MS2Stream::notifyStatsUpdated () {
	CallSessionListener *listener = getMediaSessionPrivate().getCallSessionListener();
	if (_linphone_call_stats_get_updated(mStats)) {
		switch (_linphone_call_stats_get_updated(mStats)) {
			case LINPHONE_CALL_STATS_RECEIVED_RTCP_UPDATE:
			case LINPHONE_CALL_STATS_SENT_RTCP_UPDATE:
				if (listener) {
					listener->onRtcpUpdateForReporting(getMediaSession().getSharedFromThis(), getType());
				}
				break;
			default:
				break;
		}
		if (listener)
			listener->onStatsUpdated(getMediaSession().getSharedFromThis(), mStats);
		_linphone_call_stats_set_updated(mStats, 0);
	}
}

void MS2Stream::handleEvents () {

	MediaStream *ms = getMediaStream();
	if (ms) {
		/* Ensure there is no dangling ICE check list */
		if (!getIceAgent().hasSession())
			media_stream_set_ice_check_list(ms, nullptr);
		switch(ms->type){
			case MSAudio:
				audio_stream_iterate((AudioStream *)ms);
				break;
			case MSVideo:
#ifdef VIDEO_ENABLED
				video_stream_iterate((VideoStream *)ms);
#endif
				break;
			case MSText:
				text_stream_iterate((TextStream *)ms);
				break;
			default:
				lError() << "handleStreamEvents(): unsupported stream type";
				return;
		}
	}
	OrtpEvent *ev;
	/* Yes the event queue has to be checked at each iteration, because ice events may perform operations re-creating the streams */
	while (mOrtpEvQueue && (ev = ortp_ev_queue_get(mOrtpEvQueue))) {
		OrtpEventType evt = ortp_event_get_type(ev);
		OrtpEventData *evd = ortp_event_get_data(ev);

		/*This MUST be done before any call to "linphone_call_stats_fill" since it has ownership over evd->packet*/
		if (evt == ORTP_EVENT_RTCP_PACKET_RECEIVED) {
			do {
				if (evd->packet && rtcp_is_RTPFB(evd->packet)) {
					if (rtcp_RTPFB_get_type(evd->packet) == RTCP_RTPFB_TMMBR) {
						CallSessionListener *listener = getMediaSessionPrivate().getCallSessionListener();
						listener->onTmmbrReceived(getMediaSession().getSharedFromThis(), (int)getIndex(), (int)rtcp_RTPFB_tmmbr_get_max_bitrate(evd->packet));
					}
				}
			} while (rtcp_next_packet(evd->packet));
			rtcp_rewind(evd->packet);
		}

		/* The MediaStream must be taken at each iteration, because it may have changed due to the handling of events
		 * in this loop*/
		ms = getMediaStream();
		if (ms)
			linphone_call_stats_fill(mStats, ms, ev);
		notifyStatsUpdated();
		switch(evt){
			case ORTP_EVENT_ZRTP_ENCRYPTION_CHANGED:
				if (getType() != SalAudio || !isMain()){
					getGroup().propagateEncryptionChanged();
				}
			break;
			case ORTP_EVENT_DTLS_ENCRYPTION_CHANGED:
				if (getType() != SalAudio || !isMain()){
					getGroup().propagateEncryptionChanged();
				}
			break;
			case ORTP_EVENT_ICE_SESSION_PROCESSING_FINISHED:
			case ORTP_EVENT_ICE_GATHERING_FINISHED:
			case ORTP_EVENT_ICE_LOSING_PAIRS_COMPLETED:
			case ORTP_EVENT_ICE_RESTART_NEEDED:
				/* ICE events are notified directly to the MediaSession, has it has to take actions on the signaling plane. */
				getMediaSessionPrivate().handleIceEvents(ev);
			break;
		}
	
		/* Let subclass handle the event.*/
		handleEvent(ev);
		ortp_event_destroy(ev);
	}
}

bool MS2Stream::isEncrypted() const{
	return media_stream_secured(getMediaStream());
}

RtpSession* MS2Stream::createRtpIoSession() {
	LinphoneConfig *config = linphone_core_get_config(getCCore());
	const char *config_section = getType() == SalAudio ? "sound" : "video";
	const char *rtpmap = lp_config_get_string(config, config_section, "rtp_map", getType() == SalAudio ? "pcmu/8000/1" : "vp8/90000");
	OrtpPayloadType *pt = rtp_profile_get_payload_from_rtpmap(mRtpProfile, rtpmap);
	if (!pt)
		return nullptr;
	string profileName = string("RTP IO ") + string(config_section) + string(" profile");
	mRtpIoProfile = rtp_profile_new(profileName.c_str());
	int ptnum = lp_config_get_int(config, config_section, "rtp_ptnum", 0);
	rtp_profile_set_payload(mRtpIoProfile, ptnum, payload_type_clone(pt));
	const char *localIp = lp_config_get_string(config, config_section, "rtp_local_addr", "127.0.0.1");
	int localPort = lp_config_get_int(config, config_section, "rtp_local_port", 17076);
	RtpSession *rtpSession = ms_create_duplex_rtp_session(localIp, localPort, -1, ms_factory_get_mtu(getCCore()->factory));
	rtp_session_set_profile(rtpSession, mRtpIoProfile);
	const char *remoteIp = lp_config_get_string(config, config_section, "rtp_remote_addr", "127.0.0.1");
	int remotePort = lp_config_get_int(config, config_section, "rtp_remote_port", 17078);
	rtp_session_set_remote_addr_and_port(rtpSession, remoteIp, remotePort, -1);
	rtp_session_enable_rtcp(rtpSession, false);
	rtp_session_set_payload_type(rtpSession, ptnum);
	int jittcomp = lp_config_get_int(config, config_section, "rtp_jittcomp", 0); /* 0 means no jitter buffer */
	rtp_session_set_jitter_compensation(rtpSession, jittcomp);
	rtp_session_enable_jitter_buffer(rtpSession, (jittcomp > 0));
	bool symmetric = !!lp_config_get_int(config, config_section, "rtp_symmetric", 0);
	rtp_session_set_symmetric_rtp(rtpSession, symmetric);
	return rtpSession;
}

MSZrtpContext *MS2Stream::getZrtpContext()const{
	return mSessions.zrtp_context;
}


MS2Stream::~MS2Stream(){
	linphone_call_stats_unref(mStats);
	mStats = nullptr;
	rtp_session_unregister_event_queue(mSessions.rtp_session, mOrtpEvQueue);
	ortp_ev_queue_flush(mOrtpEvQueue);
	ortp_ev_queue_destroy(mOrtpEvQueue);
	mOrtpEvQueue = nullptr;
}



LINPHONE_END_NAMESPACE
