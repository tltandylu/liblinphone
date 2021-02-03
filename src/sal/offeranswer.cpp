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

#include "c-wrapper/internal/c-tools.h"
#include "c-wrapper/internal/c-sal.h"
#include "sal/sal_stream_bundle.h"
#include "sal/sal_media_description.h"
#include "sal/sal_stream_configuration.h"
#include "sal/sal.h"
#include "offeranswer.h"
#include "private.h"

#include "utils/payload-type-handler.h"

static PayloadType * opus_match(MSOfferAnswerContext *ctx, const bctbx_list_t *local_payloads, const PayloadType *refpt, const bctbx_list_t *remote_payloads, bool_t reading_response){
	PayloadType *pt;
	const bctbx_list_t *elem;
	PayloadType *legacy_opus=NULL;

	for (elem=local_payloads;elem!=NULL;elem=elem->next){
		pt=(PayloadType*)elem->data;
		
		/*workaround a bug in earlier versions of linphone where opus/48000/1 is offered, which is uncompliant with opus rtp draft*/
		if (strcasecmp(pt->mime_type,"opus")==0 ){
			if (refpt->channels==1){
				legacy_opus=pt;
			}else if (refpt->channels==2){
				return payload_type_clone(pt);
			}
		}
	}
	if (legacy_opus){
		legacy_opus = payload_type_clone(legacy_opus);
		legacy_opus->channels=1; /*so that we respond with same number of channels */
		return legacy_opus;
	}
	return NULL;
}

static MSOfferAnswerContext *opus_offer_answer_create_context(void){
	static MSOfferAnswerContext opus_oa = {opus_match, NULL, NULL};
	return &opus_oa;
}

MSOfferAnswerProvider opus_offer_answer_provider={
	"opus",
	opus_offer_answer_create_context
};

/* the reason for this matcher is for some stupid uncompliant phone that offer G729a mime type !*/
static PayloadType * g729A_match(MSOfferAnswerContext *ctx, const bctbx_list_t *local_payloads, const PayloadType *refpt, const bctbx_list_t *remote_payloads, bool_t reading_response){
	PayloadType *pt;
	const bctbx_list_t *elem;
	PayloadType *candidate=NULL;

	for (elem=local_payloads;elem!=NULL;elem=elem->next){
		pt=(PayloadType*)elem->data;
		
		if (strcasecmp(pt->mime_type,"G729")==0 && refpt->channels==pt->channels){
			candidate=pt;
		}
	}
	return candidate ? payload_type_clone(candidate) : NULL;
}

static MSOfferAnswerContext *g729a_offer_answer_create_context(void){
	static MSOfferAnswerContext g729_oa = {g729A_match, NULL, NULL};
	return &g729_oa;
}

MSOfferAnswerProvider g729a_offer_answer_provider={
	"G729A",
	g729a_offer_answer_create_context
};

static PayloadType * red_match(MSOfferAnswerContext *ctx, const bctbx_list_t *local_payloads, const PayloadType *refpt, const bctbx_list_t *remote_payloads, bool_t reading_response) {
	const bctbx_list_t *elem_local, *elem_remote;
	PayloadType *red = NULL;

	for (elem_local = local_payloads; elem_local != NULL; elem_local = elem_local->next) {
		PayloadType *pt = (PayloadType*)elem_local->data;
		
		if (strcasecmp(pt->mime_type, payload_type_t140_red.mime_type) == 0) {
			red = payload_type_clone(pt);
			
			for (elem_remote = remote_payloads; elem_remote != NULL; elem_remote = elem_remote->next) {
				PayloadType *pt2 = (PayloadType*)elem_remote->data;
				if (strcasecmp(pt2->mime_type, payload_type_t140.mime_type) == 0) {
					int t140_payload_number = payload_type_get_number(pt2);
					char *red_fmtp = ms_strdup_printf("%i/%i/%i", t140_payload_number, t140_payload_number, t140_payload_number);
					/*modify the local payload and the return value*/
					payload_type_set_recv_fmtp(pt, red_fmtp);
					payload_type_set_recv_fmtp(red, red_fmtp);
					ms_free(red_fmtp);
					break;
				}
			}
			break;
		}
	}
	return red;
}

static MSOfferAnswerContext *red_offer_answer_create_context(void){
	static MSOfferAnswerContext red_oa = {red_match, NULL, NULL};
	return &red_oa;
}

MSOfferAnswerProvider red_offer_answer_provider={
	"red",
	red_offer_answer_create_context
};

void linphone_core_register_offer_answer_providers(LinphoneCore *lc){
	MSFactory *factory = lc->factory;
	ms_factory_register_offer_answer_provider(factory, &red_offer_answer_provider);
	ms_factory_register_offer_answer_provider(factory, &g729a_offer_answer_provider);
	ms_factory_register_offer_answer_provider(factory, &opus_offer_answer_provider);
}

LINPHONE_BEGIN_NAMESPACE

bool_t OfferAnswerEngine::onlyTelephoneEvent(const std::list<OrtpPayloadType*> & l){
	for (const auto & p : l) {
		if (strcasecmp(p->mime_type,"telephone-event")!=0){
			return FALSE;
		}
	}
	return TRUE;
}

PayloadType * OfferAnswerEngine::genericMatch(const std::list<OrtpPayloadType*> & local_payloads, const PayloadType *refpt, const std::list<OrtpPayloadType*> & remote_payloads){
	for (const auto & pt : local_payloads) {
		
		if ( pt->mime_type && refpt->mime_type 
			&& strcasecmp(pt->mime_type, refpt->mime_type)==0
			&& pt->clock_rate==refpt->clock_rate
			&& pt->channels==refpt->channels)
			return payload_type_clone(pt);
	}
	return NULL;
}

/*
 * Returns a PayloadType from the local list that matches a PayloadType offered or answered in the remote list
*/
PayloadType * OfferAnswerEngine::findPayloadTypeBestMatch(MSFactory *factory, const std::list<OrtpPayloadType*> & local_payloads, const PayloadType *refpt,
						  const std::list<OrtpPayloadType*> & remote_payloads, bool_t reading_response){
	PayloadType *ret = NULL;
	MSOfferAnswerContext *ctx = NULL;

	// When a stream is inactive, refpt->mime_type might be null
	if (refpt->mime_type && (ctx = ms_factory_create_offer_answer_context(factory, refpt->mime_type))) {
		ms_message("Doing offer/answer processing with specific provider for codec [%s]", refpt->mime_type); 
		ret = ms_offer_answer_context_match_payload(ctx, Utils::listToBctbxList(local_payloads), refpt, Utils::listToBctbxList(remote_payloads), reading_response);
		ms_offer_answer_context_destroy(ctx);
		return ret;
	}
	return OfferAnswerEngine::genericMatch(local_payloads, refpt, remote_payloads);
}


std::list<OrtpPayloadType*> OfferAnswerEngine::matchPayloads(MSFactory *factory, const std::list<OrtpPayloadType*> & local, const std::list<OrtpPayloadType*> & remote, bool_t reading_response, bool_t one_matching_codec){
	std::list<OrtpPayloadType*> res;
	PayloadType *matched;
	bool_t found_codec=FALSE;

	for (const auto & p2 : remote) {
		matched=OfferAnswerEngine::findPayloadTypeBestMatch(factory, local, p2, remote, reading_response);
		if (matched){
			int local_number=payload_type_get_number(matched);
			int remote_number=payload_type_get_number(p2);

			if (one_matching_codec){
				if (strcasecmp(matched->mime_type,"telephone-event")!=0){
					if (found_codec){/* we have found a real codec already*/
						continue; /*this codec won't be added*/
					}else found_codec=TRUE;
				}
			}

			if (p2->send_fmtp){
				payload_type_append_send_fmtp(matched,p2->send_fmtp);
			}
			payload_type_set_flag(matched, PAYLOAD_TYPE_FLAG_CAN_RECV|PAYLOAD_TYPE_FLAG_CAN_SEND);
			if (matched->flags & PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED && p2->flags & PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED) {
				payload_type_set_flag(matched, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
				/* Negotiation of AVPF features (keep common features) */
				matched->avpf.features &= p2->avpf.features;
				matched->avpf.rpsi_compatibility = p2->avpf.rpsi_compatibility;
				/* Take bigger AVPF trr interval */
				if (p2->avpf.trr_interval > matched->avpf.trr_interval) {
					matched->avpf.trr_interval = p2->avpf.trr_interval;
				}
			}else{
				payload_type_unset_flag(matched, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
			}
			res.push_back(matched);
			/* we should use the remote numbering even when parsing a response */
			payload_type_set_number(matched,remote_number);
			payload_type_set_flag(matched, PAYLOAD_TYPE_FROZEN_NUMBER);
			if (reading_response && remote_number!=local_number){
				ms_warning("For payload type %s, proposed number was %i but the remote phone answered %i",
						  matched->mime_type, local_number, remote_number);
				/*
				 We must add this payload type with our local numbering in order to be able to receive it.
				 Indeed despite we must sent with the remote numbering, we must be able to receive with
				 our local one.
				*/
				matched=payload_type_clone(matched);
				payload_type_set_number(matched,local_number);
				payload_type_set_flag(matched, PAYLOAD_TYPE_FLAG_CAN_RECV);
				payload_type_set_flag(matched, PAYLOAD_TYPE_FROZEN_NUMBER);
				res.push_back(matched);
			}
		}else{
			if (p2->channels>0)
				ms_message("No match for %s/%i/%i",p2->mime_type,p2->clock_rate,p2->channels);
			else ms_message("No match for %s/%i",p2->mime_type,p2->clock_rate);
		}
	}
	if (reading_response){
		/* add remaning local payload as CAN_RECV only so that if we are in front of a non-compliant equipment we are still able to decode the RTP stream*/
		for (const auto & p1 : local) {
			bool_t found=FALSE;
			for (const auto & p2 : remote) {
				if (payload_type_get_number(p2)==payload_type_get_number(p1)){
					found=TRUE;
					break;
				}
			}
			if (!found){
				ms_message("Adding %s/%i for compatibility, just in case.",p1->mime_type,p1->clock_rate);
				PayloadType *cloned_p1=payload_type_clone(p1);
				payload_type_set_flag(cloned_p1, PAYLOAD_TYPE_FLAG_CAN_RECV);
				payload_type_set_flag(cloned_p1, PAYLOAD_TYPE_FROZEN_NUMBER);
				res.push_back(cloned_p1);
			}
		}
	}
	return res;
}

bool_t OfferAnswerEngine::matchCryptoAlgo(const std::vector<SalSrtpCryptoAlgo> &local, const std::vector<SalSrtpCryptoAlgo> &remote,
	SalSrtpCryptoAlgo & result, unsigned int & choosen_local_tag, bool_t use_local_key) {
	for(const auto & rc : remote) {
		if (rc.algo == 0)
			break;

		/* Look for a local enabled crypto algo that matches one of the proposed by remote */
		for(const auto & lc : local) {
			if (rc.algo == lc.algo) {
				result.algo = rc.algo;
				/* We're answering an SDP offer. Supply our master key, associated with the remote supplied tag */
				if (use_local_key) {
					result.master_key = lc.master_key;
					result.tag = rc.tag;
					choosen_local_tag = lc.tag;
				}
				/* We received an answer to our SDP crypto proposal. Copy matching algo remote master key to result, and memorize local tag */
				else {
					result.master_key = rc.master_key;
					result.tag = lc.tag;
					choosen_local_tag = lc.tag;
				}
				return TRUE;
			}
		}
	}
	return FALSE;
}

SalStreamDir OfferAnswerEngine::computeDirOutgoing(SalStreamDir local, SalStreamDir answered){
	SalStreamDir res=local;
	if (local==SalStreamSendRecv){
		if (answered==SalStreamRecvOnly){
			res=SalStreamSendOnly;
		}else if (answered==SalStreamSendOnly){
			res=SalStreamRecvOnly;
		}
	}
	if (answered==SalStreamInactive){
		res=SalStreamInactive;
	}
	return res;
}

SalStreamDir OfferAnswerEngine::computeDirIncoming(SalStreamDir local, SalStreamDir offered){
	SalStreamDir res=SalStreamSendRecv;
	if (local==SalStreamSendRecv){
		if (offered==SalStreamSendOnly)
			res=SalStreamRecvOnly;
		else if (offered==SalStreamRecvOnly)
			res=SalStreamSendOnly;
		else if (offered==SalStreamInactive)
			res=SalStreamInactive;
		else
			res=SalStreamSendRecv;
	}else if (local==SalStreamSendOnly){
		if (offered==SalStreamRecvOnly || offered==SalStreamSendRecv)
			res=SalStreamSendOnly;
		else res=SalStreamInactive;
	}else if (local==SalStreamRecvOnly){
		if (offered==SalStreamSendOnly || offered==SalStreamSendRecv)
			res=SalStreamRecvOnly;
		else
			res=SalStreamInactive;
	}else res=SalStreamInactive;
	return res;
}

void OfferAnswerEngine::initiateOutgoingStream(MSFactory* factory, const SalStreamDescription & local_offer,
						const SalStreamDescription & remote_answer,
						SalStreamDescription & result){
	SalStreamConfiguration cfg = result.getActualConfiguration();
	if (remote_answer.enabled())
		cfg.payloads=OfferAnswerEngine::matchPayloads(factory, local_offer.getChosenConfiguration().payloads,remote_answer.getChosenConfiguration().payloads,TRUE,FALSE);
	else {
		ms_message("Local stream description [%p] rejected by peer",&local_offer);
		result.rtp_port=0;
		return;
	}
	cfg.proto=remote_answer.getProto();
	result.type=local_offer.getType();

	if (local_offer.rtp_addr.empty() == false && ms_is_multicast(L_STRING_TO_C(local_offer.rtp_addr))) {
			/*6.2 Multicast Streams
			...
		If a multicast stream is accepted, the address and port information
		in the answer MUST match that of the offer.  Similarly, the
		directionality information in the answer (sendonly, recvonly, or
		sendrecv) MUST equal that of the offer.  This is because all
		participants in a multicast session need to have equivalent views of
		the parameters of the session, an underlying assumption of the
		multicast bias of RFC 2327.*/
		if (local_offer.rtp_addr.compare(remote_answer.rtp_addr) !=0 ) {
			ms_message("Remote answered IP [%s] does not match offered [%s] for local stream description [%p]"
																,L_STRING_TO_C(remote_answer.rtp_addr)
																,L_STRING_TO_C(local_offer.rtp_addr)
																,&local_offer);
			result.rtp_port=0;
			return;
		}
		if (local_offer.rtp_port!=remote_answer.rtp_port) {
			ms_message("Remote answered rtp port [%i] does not match offered [%i] for local stream description [%p]"
																,remote_answer.rtp_port
																,local_offer.rtp_port
																,&local_offer);
			result.rtp_port=0;
			return;
		}
		if (local_offer.getDirection()!=remote_answer.getDirection()) {
			ms_message("Remote answered dir [%s] does not match offered [%s] for local stream description [%p]"
																,sal_stream_dir_to_string(remote_answer.getDirection())
																,sal_stream_dir_to_string(local_offer.getDirection())
																,&local_offer);
			result.rtp_port=0;
			return;
		}
		if (local_offer.bandwidth!=remote_answer.bandwidth) {
			ms_message("Remote answered bandwidth [%i] does not match offered [%i] for local stream description [%p]"
																,remote_answer.bandwidth
																,local_offer.bandwidth
																,&local_offer);
			result.rtp_port=0;
			return;
		}
		if (local_offer.getChosenConfiguration().ptime > 0 && local_offer.getChosenConfiguration().ptime!=remote_answer.getChosenConfiguration().ptime) {
			ms_message("Remote answered ptime [%i] does not match offered [%i] for local stream description [%p]"
																,remote_answer.getChosenConfiguration().ptime
																,local_offer.getChosenConfiguration().ptime
																,&local_offer);
			result.rtp_port=0;
			return;
		}
		if (local_offer.getChosenConfiguration().ttl > 0 && local_offer.getChosenConfiguration().ttl!=remote_answer.getChosenConfiguration().ttl) {
			ms_message("Remote answered ttl [%i] does not match offered [%i] for local stream description [%p]"
																		,remote_answer.getChosenConfiguration().ttl
																		,local_offer.getChosenConfiguration().ttl
																		,&local_offer);
			result.rtp_port=0;
			return;
		}
		cfg.ttl=local_offer.getChosenConfiguration().ttl;
		cfg.dir=local_offer.getDirection();
		result.multicast_role = SalMulticastSender;
	} else {
		cfg.dir=OfferAnswerEngine::computeDirOutgoing(local_offer.getDirection(),remote_answer.getDirection());
	}

	cfg.rtcp_mux = remote_answer.getChosenConfiguration().rtcp_mux && local_offer.getChosenConfiguration().rtcp_mux;
	if (remote_answer.getChosenConfiguration().mid.empty() == false){
		if (local_offer.getChosenConfiguration().mid.empty() == false){
			cfg.mid = remote_answer.getChosenConfiguration().mid;
			cfg.mid_rtp_ext_header_id = remote_answer.getChosenConfiguration().mid_rtp_ext_header_id;
			cfg.bundle_only = remote_answer.getChosenConfiguration().bundle_only;
			cfg.rtcp_mux = TRUE; /* RTCP mux must be enabled in bundle mode. */
		}else{
			ms_error("The remote has set a mid in an answer while we didn't offered it.");
		}
	}

	if (!cfg.payloads.empty() && !OfferAnswerEngine::onlyTelephoneEvent(cfg.payloads)){
		result.rtp_addr=remote_answer.rtp_addr;
		result.rtcp_addr=remote_answer.rtcp_addr;
		result.rtp_port=remote_answer.rtp_port;
		result.rtcp_port=remote_answer.rtcp_port;
		result.bandwidth=remote_answer.bandwidth;
		cfg.ptime=remote_answer.getChosenConfiguration().ptime;
		cfg.maxptime=remote_answer.getChosenConfiguration().maxptime;
	}else{
		result.disable();
	}
	if (cfg.hasSrtp() == TRUE) {
		/* verify crypto algo */
		cfg.crypto.clear();
		SalSrtpCryptoAlgo crypto_result;
		if (!OfferAnswerEngine::matchCryptoAlgo(local_offer.getChosenConfiguration().crypto, remote_answer.getChosenConfiguration().crypto, crypto_result, cfg.crypto_local_tag, FALSE)) {
			result.disable();
		}
		if (cfg.crypto.empty()) {
			cfg.crypto.resize(1);
		}
		cfg.crypto[0] = crypto_result;
	}
	cfg.rtp_ssrc=local_offer.getChosenConfiguration().rtp_ssrc;
	cfg.rtcp_cname=local_offer.getChosenConfiguration().rtcp_cname;

	// Handle dtls session attribute: if both local and remote have a dtls fingerprint and a dtls setup, get the remote fingerprint into the result
	if ((local_offer.getChosenConfiguration().dtls_role!=SalDtlsRoleInvalid) && (remote_answer.getChosenConfiguration().dtls_role!=SalDtlsRoleInvalid)
			&&(!local_offer.getChosenConfiguration().dtls_fingerprint.empty()) && (!remote_answer.getChosenConfiguration().dtls_fingerprint.empty())) {
		cfg.dtls_fingerprint = remote_answer.getChosenConfiguration().dtls_fingerprint;
		if (remote_answer.getChosenConfiguration().dtls_role==SalDtlsRoleIsClient) {
			cfg.dtls_role = SalDtlsRoleIsServer;
		} else {
			cfg.dtls_role = SalDtlsRoleIsClient;
		}
	} else {
		cfg.dtls_fingerprint.clear();
		cfg.dtls_role = SalDtlsRoleInvalid;
	}
	cfg.implicit_rtcp_fb = local_offer.getChosenConfiguration().implicit_rtcp_fb && remote_answer.getChosenConfiguration().implicit_rtcp_fb;

	result.addActualConfiguration(cfg);
}

void OfferAnswerEngine::initiateIncomingStream(MSFactory *factory, const SalStreamDescription & local_cap,
						const SalStreamDescription & remote_offer,
						SalStreamDescription & result, bool_t one_matching_codec, const char *bundle_owner_mid){
	SalStreamConfiguration cfg = result.getActualConfiguration();
	cfg.payloads=OfferAnswerEngine::matchPayloads(factory, local_cap.getChosenConfiguration().payloads,remote_offer.getChosenConfiguration().payloads, FALSE, one_matching_codec);
	cfg.proto=remote_offer.getProto();
	result.type=local_cap.getType();
	cfg.dir=OfferAnswerEngine::computeDirIncoming(local_cap.getDirection(),remote_offer.getDirection());
	
	if (cfg.payloads.empty() || OfferAnswerEngine::onlyTelephoneEvent(cfg.payloads) || !remote_offer.enabled()){
		result.rtp_port=0;
		return;
	}
	if (remote_offer.rtp_addr.empty() == false && ms_is_multicast(L_STRING_TO_C(remote_offer.rtp_addr))) {
		if (cfg.hasSrtp() == TRUE) {
			ms_message("SAVP not supported for multicast address for remote stream [%p]",&remote_offer);
			result.rtp_port=0;
			return;
		}
		cfg.dir=remote_offer.getDirection();
		result.rtp_addr=remote_offer.rtp_addr;
		result.rtcp_addr=remote_offer.rtcp_addr;
		result.rtp_port=remote_offer.rtp_port;
		/*result.rtcp_port=remote_offer.rtcp_port;*/
		result.rtcp_port=0; /* rtcp not supported yet*/
		result.bandwidth=remote_offer.bandwidth;
		cfg.ptime=remote_offer.getChosenConfiguration().ptime;
		cfg.maxptime=remote_offer.getChosenConfiguration().maxptime;
		cfg.ttl=remote_offer.getChosenConfiguration().ttl;
		result.multicast_role = SalMulticastReceiver;
	} else {
		result.rtp_addr=local_cap.rtp_addr;
		result.rtcp_addr=local_cap.rtcp_addr;
		result.rtp_port=local_cap.rtp_port;
		result.rtcp_port=local_cap.rtcp_port;
		result.bandwidth=local_cap.bandwidth;
		cfg.ptime=local_cap.getChosenConfiguration().ptime;
		cfg.maxptime=local_cap.getChosenConfiguration().maxptime;
	}
	
	cfg.rtcp_mux = remote_offer.getChosenConfiguration().rtcp_mux && local_cap.getChosenConfiguration().rtcp_mux;
	
	/* Handle RTP bundle negociation */
	if (!remote_offer.getChosenConfiguration().mid.empty() && bundle_owner_mid){
		cfg.mid = remote_offer.getChosenConfiguration().mid;
		cfg.mid_rtp_ext_header_id = remote_offer.getChosenConfiguration().mid_rtp_ext_header_id;
		
		if (remote_offer.getChosenConfiguration().mid.compare(bundle_owner_mid) != 0){
			/* The stream is a secondary one part of a bundle.
			 * In this case it must set the bundle-only attribute, and set port to zero.*/
			cfg.bundle_only = TRUE;
			result.rtp_port = 0;
		}
		cfg.rtcp_mux = TRUE; /* RTCP mux must be enabled in bundle mode. */
	}

	if (cfg.hasSrtp() == TRUE) {
		/* select crypto algo */
		cfg.crypto.clear();
		SalSrtpCryptoAlgo crypto_result;
		if (!OfferAnswerEngine::matchCryptoAlgo(local_cap.getChosenConfiguration().crypto, remote_offer.getChosenConfiguration().crypto, crypto_result, cfg.crypto_local_tag, TRUE)) {
			result.disable();
			ms_message("No matching crypto algo for remote stream's offer [%p]",&remote_offer);
		}
		if (cfg.crypto.empty()) {
			cfg.crypto.resize(1);
		}
		cfg.crypto[0] = crypto_result;
	}

	// add our zrtp hash if remote gave one but also when our side has set ZRTP as active to help peer starting earlier if it has ZRTP capabilities
	// haveZrtpHash is set in local_cap when ZRTP is active on our side.
	if ((remote_offer.getChosenConfiguration().haveZrtpHash == 1) || (local_cap.getChosenConfiguration().haveZrtpHash == 1)) {
		if (local_cap.getChosenConfiguration().zrtphash[0] != 0) { /* if ZRTP is available, set the zrtp hash even if it is not selected */
			strncpy((char *)(cfg.zrtphash), (char *)(local_cap.getChosenConfiguration().zrtphash), sizeof(cfg.zrtphash));
			cfg.haveZrtpHash =  1;
		}
	}

	cfg.ice_pwd = local_cap.getChosenConfiguration().ice_pwd;
	cfg.ice_ufrag = local_cap.getChosenConfiguration().ice_ufrag;
	cfg.ice_mismatch = local_cap.getChosenConfiguration().ice_mismatch;
	cfg.set_nortpproxy = local_cap.getChosenConfiguration().set_nortpproxy;
	cfg.ice_candidates = local_cap.getChosenConfiguration().ice_candidates;
	cfg.ice_remote_candidates = local_cap.getChosenConfiguration().ice_remote_candidates;
	result.name = local_cap.name;
	cfg.rtp_ssrc=local_cap.getChosenConfiguration().rtp_ssrc;
	cfg.rtcp_cname=local_cap.getChosenConfiguration().rtcp_cname;

	// Handle dtls stream attribute: if both local and remote have a dtls fingerprint and a dtls setup, add the local fingerprint to the answer
	// Note: local description usually stores dtls config at session level which means it apply to all streams, check this too
	if (((local_cap.getChosenConfiguration().dtls_role!=SalDtlsRoleInvalid)) && (remote_offer.getChosenConfiguration().dtls_role!=SalDtlsRoleInvalid)
			&& (!local_cap.getChosenConfiguration().dtls_fingerprint.empty()) && (!remote_offer.getChosenConfiguration().dtls_fingerprint.empty())) {
		cfg.dtls_fingerprint = local_cap.getChosenConfiguration().dtls_fingerprint;
		if (remote_offer.getChosenConfiguration().dtls_role==SalDtlsRoleUnset) {
			cfg.dtls_role = SalDtlsRoleIsClient;
		}
	} else {
		cfg.dtls_fingerprint.clear();
		cfg.dtls_role = SalDtlsRoleInvalid;
	}
	cfg.implicit_rtcp_fb = local_cap.getChosenConfiguration().implicit_rtcp_fb && remote_offer.getChosenConfiguration().implicit_rtcp_fb;
	result.addActualConfiguration(cfg);
}

bool OfferAnswerEngine::areProtoCompatibles(SalMediaProto localProto, SalMediaProto otherProto)
{
	switch (localProto) {
		case SalProtoRtpAvp:
		case SalProtoRtpAvpf:
			return (otherProto == SalProtoRtpAvp || otherProto == SalProtoRtpAvpf);
		case SalProtoRtpSavp:
		case SalProtoRtpSavpf:
			return (otherProto == SalProtoRtpSavp || otherProto == SalProtoRtpSavpf);
		case SalProtoUdpTlsRtpSavp:
		case SalProtoUdpTlsRtpSavpf:
			return (otherProto == SalProtoUdpTlsRtpSavp || otherProto == SalProtoUdpTlsRtpSavpf);
		default:
			return (otherProto == localProto);
	}
}

/**
 * Returns a media description to run the streams with, based on a local offer
 * and the returned response (remote).
**/
int OfferAnswerEngine::initiateOutgoing(MSFactory *factory, std::shared_ptr<SalMediaDescription> local_offer,
					const std::shared_ptr<SalMediaDescription> remote_answer,
					std::shared_ptr<SalMediaDescription> result){
	size_t i;

	for(i=0;i<local_offer->streams.size();++i){
		ms_message("Processing for stream %zu",i);
		SalStreamDescription & ls = local_offer->streams[i];
		const SalStreamDescription & rs = remote_answer->streams[i];
		if ((i < remote_answer->streams.size()) && rs.getType() == ls.getType() && OfferAnswerEngine::areProtoCompatibles(ls.getProto(), rs.getProto()))
		{
			if (ls.getProto() != rs.getProto() && ls.hasAvpf()) {
				ls.setProto(rs.getProto());
				ms_warning("Received a downgraded AVP answer for our AVPF offer");
			}
			if (i <= result->streams.size()) {
				result->streams.resize((i + 1));
			}
			OfferAnswerEngine::initiateOutgoingStream(factory, ls,rs,result->streams[i]);
			SalStreamConfiguration cfg = result->streams[i].getActualConfiguration();
			memcpy(&cfg.rtcp_xr, &ls.getChosenConfiguration().rtcp_xr, sizeof(result->streams[i].getChosenConfiguration().rtcp_xr));
			if ((ls.getChosenConfiguration().rtcp_xr.enabled == TRUE) && (rs.getChosenConfiguration().rtcp_xr.enabled == FALSE)) {
				cfg.rtcp_xr.enabled = FALSE;
			}
			cfg.rtcp_fb.generic_nack_enabled = ls.getChosenConfiguration().rtcp_fb.generic_nack_enabled & rs.getChosenConfiguration().rtcp_fb.generic_nack_enabled;
			cfg.rtcp_fb.tmmbr_enabled = ls.getChosenConfiguration().rtcp_fb.tmmbr_enabled & rs.getChosenConfiguration().rtcp_fb.tmmbr_enabled;
			result->streams[i].addActualConfiguration(cfg);
		}
		else ms_warning("No matching stream for %zu",i);
	}
	result->bandwidth=remote_answer->bandwidth;
	result->addr=remote_answer->addr;
	result->ice_pwd = local_offer->ice_pwd;
	result->ice_ufrag = local_offer->ice_ufrag;
	memcpy(&result->rtcp_xr, &local_offer->rtcp_xr, sizeof(result->rtcp_xr));
	if ((local_offer->rtcp_xr.enabled == TRUE) && (remote_answer->rtcp_xr.enabled == FALSE)) {
		result->rtcp_xr.enabled = FALSE;
	}
	/* TODO: check that the bundle answer is compliant with our offer.
	 * For now, just check the presence of a bundle response. */
	if (!local_offer->bundles.empty()){
		if (!remote_answer->bundles.empty()){
			/* Copy the bundle offering to the result media description. */
			result->bundles = remote_answer->bundles;
		}
	}else if (!remote_answer->bundles.empty()){
		ms_error("Remote answerer is proposing bundles, which we did not offer.");
	}

	return 0;
}

/**
 * Returns a media description to run the streams with, based on the local capabilities and
 * and the received offer.
 * The returned media description is an answer and should be sent to the offerer.
**/
int OfferAnswerEngine::initiateIncoming(MSFactory *factory, const std::shared_ptr<SalMediaDescription> local_capabilities,
					std::shared_ptr<SalMediaDescription> remote_offer,
					std::shared_ptr<SalMediaDescription> result, bool_t one_matching_codec){
	size_t i;

	if (!remote_offer->bundles.empty() && local_capabilities->accept_bundles){
		/* Copy the bundle offering to the result media description. */
		result->bundles = remote_offer->bundles;
	}

	if (result->streams.size() < remote_offer->streams.size()) {
		result->streams.resize(remote_offer->streams.size());
	}

	for(i=0;i<remote_offer->streams.size();++i){

		if (i >= local_capabilities->streams.size()) {
			local_capabilities->streams.resize((i + 1));
		}
		const SalStreamDescription & ls = local_capabilities->streams[i];
		SalStreamDescription & rs = remote_offer->streams[i];
		SalStreamDescription & resultStream = result->streams[i];

		SalStreamConfiguration cfg = resultStream.getActualConfiguration();

		if (rs.getType() == ls.getType() && OfferAnswerEngine::areProtoCompatibles(ls.getProto(), rs.getProto()))
		{
			if (ls.getProto() != rs.getProto() && rs.hasAvpf())	{
				rs.setProto(ls.getProto());
				ms_warning("Sending a downgraded AVP answer for the received AVPF offer");
			}
			const char *bundle_owner_mid = NULL;
			if (local_capabilities->accept_bundles){
				int owner_index = remote_offer->getIndexOfTransportOwner(rs);
				if (owner_index != -1){
					bundle_owner_mid = L_STRING_TO_C(remote_offer->streams[(size_t)owner_index].getChosenConfiguration().getMid());
				}
			}
			OfferAnswerEngine::initiateIncomingStream(factory, ls,rs,resultStream,one_matching_codec, bundle_owner_mid);
			// Get an up to date actual configuration as it may have changed
			cfg = resultStream.getActualConfiguration();
			// Handle global RTCP FB attributes
			cfg.rtcp_fb.generic_nack_enabled = rs.getChosenConfiguration().rtcp_fb.generic_nack_enabled;
			cfg.rtcp_fb.tmmbr_enabled = rs.getChosenConfiguration().rtcp_fb.tmmbr_enabled;
			// Handle media RTCP XR attribute
			memset(&cfg.rtcp_xr, 0, sizeof(cfg.rtcp_xr));
			if (rs.getChosenConfiguration().rtcp_xr.enabled == TRUE) {
				const OrtpRtcpXrConfiguration *rtcp_xr_conf = NULL;
				if (ls.getChosenConfiguration().rtcp_xr.enabled == TRUE) rtcp_xr_conf = &ls.getChosenConfiguration().rtcp_xr;
				else if (local_capabilities->rtcp_xr.enabled == TRUE) rtcp_xr_conf = &local_capabilities->rtcp_xr;
				if ((rtcp_xr_conf != NULL) && (ls.getDirection() == SalStreamSendRecv)) {
					memcpy(&cfg.rtcp_xr, rtcp_xr_conf, sizeof(cfg.rtcp_xr));
				} else {
					cfg.rtcp_xr.enabled = TRUE;
				}
			}
		}else {
			ms_message("Declining mline %zu, no corresponding stream in local capabilities description.",i);
			/* create an inactive stream for the answer, as there where no matching stream in local capabilities */
			cfg.dir=SalStreamInactive;
			resultStream.rtp_port=0;
			resultStream.type=rs.getType();
			cfg.proto=rs.getProto();
			if (rs.getType()==SalOther){
				resultStream.typeother = rs.typeother;
			}
			if (rs.getProto()==SalProtoOther){
				cfg.proto_other = rs.getChosenConfiguration().proto_other;
			}
		}
		cfg.custom_sdp_attributes = sal_custom_sdp_attribute_clone(ls.getChosenConfiguration().custom_sdp_attributes);
		resultStream.addActualConfiguration(cfg);
	}
	result->username=local_capabilities->username;
	result->addr=local_capabilities->addr;
	result->bandwidth=local_capabilities->bandwidth;
	result->session_ver=local_capabilities->session_ver;
	result->session_id=local_capabilities->session_id;
	result->ice_pwd = local_capabilities->ice_pwd;
	result->ice_ufrag = local_capabilities->ice_ufrag;
	result->ice_lite = local_capabilities->ice_lite;
	result->set_nortpproxy = local_capabilities->set_nortpproxy;
	result->custom_sdp_attributes = sal_custom_sdp_attribute_clone(local_capabilities->custom_sdp_attributes);

	result->name=local_capabilities->name;

	// Handle session RTCP XR attribute
	memset(&result->rtcp_xr, 0, sizeof(result->rtcp_xr));
	if (remote_offer->rtcp_xr.enabled == TRUE) {
		if ((local_capabilities->rtcp_xr.enabled == TRUE) && (local_capabilities->dir == SalStreamSendRecv)) {
			memcpy(&result->rtcp_xr, &local_capabilities->rtcp_xr, sizeof(result->rtcp_xr));
		} else {
			result->rtcp_xr.enabled = TRUE;
		}
	}

	return 0;
}

LINPHONE_END_NAMESPACE