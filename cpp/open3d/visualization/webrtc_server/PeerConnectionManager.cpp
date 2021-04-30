// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2021 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Contains source code from
// https://github.com/mpromonet/webrtc-streamer
//
// This software is in the public domain, furnished "as is", without technical
// support, and with no warranty, express or implied, as to its usefulness for
// any purpose.
// ----------------------------------------------------------------------------

#include "open3d/visualization/webrtc_server/PeerConnectionManager.h"

#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/fake_audio_device.h>
#include <p2p/client/basic_port_allocator.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <utility>

#include "open3d/utility/IJsonConvertible.h"
#include "open3d/visualization/webrtc_server/BitmapTrackSource.h"
#include "open3d/visualization/webrtc_server/ImageCapturer.h"
#include "open3d/visualization/webrtc_server/VideoFilter.h"
#include "open3d/visualization/webrtc_server/VideoScaler.h"

namespace open3d {
namespace visualization {
namespace webrtc_server {

// Names used for a IceCandidate JSON object.
const char k_candidate_sdp_mid_name[] = "sdpMid";
const char k_candidate_sdp_mline_index_name[] = "sdpMLineIndex";
const char k_candidate_sdp_name[] = "candidate";

// Names used for a SessionDescription JSON object.
const char k_session_description_type_name[] = "type";
const char k_session_description_sdp_name[] = "sdp";

struct IceServer {
    std::string url;
    std::string user;
    std::string pass;
};

static IceServer GetIceServerFromUrl(const std::string &url) {
    IceServer srv;
    srv.url = url;

    std::size_t pos = url.find_first_of(':');
    if (pos != std::string::npos) {
        std::string protocol = url.substr(0, pos);
        std::string uri = url.substr(pos + 1);
        std::string credentials;

        std::size_t pos = uri.rfind('@');
        if (pos != std::string::npos) {
            credentials = uri.substr(0, pos);
            uri = uri.substr(pos + 1);
        }
        srv.url = protocol + ":" + uri;

        if (!credentials.empty()) {
            pos = credentials.find(':');
            if (pos == std::string::npos) {
                srv.user = credentials;
            } else {
                srv.user = credentials.substr(0, pos);
                srv.pass = credentials.substr(pos + 1);
            }
        }
    }

    return srv;
}

webrtc::PeerConnectionFactoryDependencies
CreatePeerConnectionFactoryDependencies() {
    webrtc::PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = nullptr;
    dependencies.worker_thread = rtc::Thread::Current();
    dependencies.signaling_thread = nullptr;
    dependencies.call_factory = webrtc::CreateCallFactory();
    dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
    dependencies.event_log_factory =
            absl::make_unique<webrtc::RtcEventLogFactory>(
                    dependencies.task_queue_factory.get());

    cricket::MediaEngineDependencies media_dependencies;
    media_dependencies.task_queue_factory =
            dependencies.task_queue_factory.get();

    // Dummy audio factory.
    rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module(
            new webrtc::FakeAudioDeviceModule());
    media_dependencies.adm = std::move(audio_device_module);
    media_dependencies.audio_encoder_factory =
            webrtc::CreateBuiltinAudioEncoderFactory();
    media_dependencies.audio_decoder_factory =
            webrtc::CreateBuiltinAudioDecoderFactory();
    media_dependencies.audio_processing =
            webrtc::AudioProcessingBuilder().Create();

    media_dependencies.video_encoder_factory =
            webrtc::CreateBuiltinVideoEncoderFactory();
    media_dependencies.video_decoder_factory =
            webrtc::CreateBuiltinVideoDecoderFactory();

    dependencies.media_engine =
            cricket::CreateMediaEngine(std::move(media_dependencies));

    return dependencies;
}

PeerConnectionManager::PeerConnectionManager(
        WebRTCServer *webrtc_server,
        const std::list<std::string> &ice_server_list,
        const Json::Value &config,
        const std::string &publish_filter,
        const std::string &webrtc_udp_port_range)
    : webrtc_server_(webrtc_server),
      task_queue_factory_(webrtc::CreateDefaultTaskQueueFactory()),
      peer_connection_factory_(webrtc::CreateModularPeerConnectionFactory(
              CreatePeerConnectionFactoryDependencies())),
      ice_server_list_(ice_server_list),
      config_(config),
      publish_filter_(publish_filter) {
    // Set the webrtc port range.
    webrtc_port_range_ = webrtc_udp_port_range;

    // Register api in http server.
    func_["/api/getMediaList"] = [this](const struct mg_request_info *req_info,
                                        const Json::Value &in) -> Json::Value {
        return this->GetMediaList();
    };

    func_["/api/getIceServers"] = [this](const struct mg_request_info *req_info,
                                         const Json::Value &in) -> Json::Value {
        return this->GetIceServers();
    };

    func_["/api/call"] = [this](const struct mg_request_info *req_info,
                                const Json::Value &in) -> Json::Value {
        std::string peerid;
        std::string url;  // window_uid.
        std::string options;
        if (req_info->query_string) {
            CivetServer::getParam(req_info->query_string, "peerid", peerid);
            CivetServer::getParam(req_info->query_string, "url", url);
            CivetServer::getParam(req_info->query_string, "options", options);
        }
        utility::LogInfo("/api/call in data ////////////////////////////////");
        utility::LogInfo("{}", utility::JsonToString(in));
        utility::LogInfo("//////////////////////////////////////////////////");
        return this->Call(peerid, url, options, in);
    };

    func_["/api/hangup"] = [this](const struct mg_request_info *req_info,
                                  const Json::Value &in) -> Json::Value {
        std::string peerid;
        if (req_info->query_string) {
            CivetServer::getParam(req_info->query_string, "peerid", peerid);
        }
        return this->HangUp(peerid);
    };

    func_["/api/createOffer"] = [this](const struct mg_request_info *req_info,
                                       const Json::Value &in) -> Json::Value {
        std::string peerid;
        std::string url;
        std::string options;
        if (req_info->query_string) {
            CivetServer::getParam(req_info->query_string, "peerid", peerid);
            CivetServer::getParam(req_info->query_string, "url", url);
            CivetServer::getParam(req_info->query_string, "options", options);
        }
        return this->CreateOffer(peerid, url, options);
    };

    func_["/api/getIceCandidate"] =
            [this](const struct mg_request_info *req_info,
                   const Json::Value &in) -> Json::Value {
        std::string peerid;
        if (req_info->query_string) {
            CivetServer::getParam(req_info->query_string, "peerid", peerid);
        }
        return this->GetIceCandidateList(peerid);
    };

    func_["/api/addIceCandidate"] =
            [this](const struct mg_request_info *req_info,
                   const Json::Value &in) -> Json::Value {
        std::string peerid;
        if (req_info->query_string) {
            CivetServer::getParam(req_info->query_string, "peerid", peerid);
        }
        return this->AddIceCandidate(peerid, in);
    };
}

PeerConnectionManager::~PeerConnectionManager() {}

// Return deviceList as JSON vector.
const Json::Value PeerConnectionManager::GetMediaList() {
    Json::Value value(Json::arrayValue);

    for (const std::string &window_uid : webrtc_server_->GetWindowUIDs()) {
        Json::Value media;
        media["video"] = window_uid;
        value.append(media);
    }

    return value;
}

// Return iceServers as JSON vector.
const Json::Value PeerConnectionManager::GetIceServers() {
    // This is a simplified version. The original version takes the client's IP
    // and the server returns the best available STUN server.
    Json::Value urls(Json::arrayValue);

    for (auto ice_server : ice_server_list_) {
        Json::Value server;
        Json::Value urlList(Json::arrayValue);
        IceServer srv = GetIceServerFromUrl(ice_server);
        RTC_LOG(INFO) << "ICE URL:" << srv.url;
        urlList.append(srv.url);
        server["urls"] = urlList;
        if (srv.user.length() > 0) server["username"] = srv.user;
        if (srv.pass.length() > 0) server["credential"] = srv.pass;
        urls.append(server);
    }

    Json::Value iceServers;
    iceServers["iceServers"] = urls;

    return iceServers;
}

// Get PeerConnection associated with peerid.
rtc::scoped_refptr<webrtc::PeerConnectionInterface>
PeerConnectionManager::GetPeerConnection(const std::string &peerid) {
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
    std::map<std::string, PeerConnectionObserver *>::iterator it =
            peer_id_to_connection_.find(peerid);
    if (it != peer_id_to_connection_.end()) {
        peer_connection = it->second->GetPeerConnection();
    }
    return peer_connection;
}

// Add ICE candidate to a PeerConnection.
const Json::Value PeerConnectionManager::AddIceCandidate(
        const std::string &peerid, const Json::Value &jmessage) {
    bool result = false;
    std::string sdp_mid;
    int sdp_mlineindex = 0;
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(jmessage, k_candidate_sdp_mid_name,
                                      &sdp_mid) ||
        !rtc::GetIntFromJsonObject(jmessage, k_candidate_sdp_mline_index_name,
                                   &sdp_mlineindex) ||
        !rtc::GetStringFromJsonObject(jmessage, k_candidate_sdp_name, &sdp)) {
        RTC_LOG(WARNING) << "Can't parse received message:" << jmessage;
    } else {
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
                webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp,
                                           nullptr));
        if (!candidate.get()) {
            RTC_LOG(WARNING) << "Can't parse received candidate message.";
        } else {
            std::lock_guard<std::mutex> peerlock(peer_id_to_connection_mutex_);
            rtc::scoped_refptr<webrtc::PeerConnectionInterface>
                    peer_connection = this->GetPeerConnection(peerid);
            if (peer_connection) {
                if (!peer_connection->AddIceCandidate(candidate.get())) {
                    RTC_LOG(WARNING)
                            << "Failed to apply the received candidate";
                } else {
                    result = true;
                }
            }
        }
    }
    Json::Value answer;
    if (result) {
        answer = result;
    }
    return answer;
}

// Create an offer for a call,
const Json::Value PeerConnectionManager::CreateOffer(
        const std::string &peerid,
        const std::string &window_uid,
        const std::string &options) {
    RTC_LOG(INFO) << __FUNCTION__ << " video:" << window_uid
                  << " options:" << options;
    Json::Value offer;

    PeerConnectionObserver *peer_connection_observer =
            this->CreatePeerConnection(peerid);
    if (!peer_connection_observer) {
        RTC_LOG(LERROR) << "Failed to initialize PeerConnection";
    } else {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection =
                peer_connection_observer->GetPeerConnection();

        if (!this->AddStreams(peer_connection, window_uid, options)) {
            RTC_LOG(WARNING) << "Can't add stream";
        }

        // Register peerid.
        {
            std::lock_guard<std::mutex> peerlock(peer_id_to_connection_mutex_);
            peer_id_to_connection_.insert(
                    std::pair<std::string, PeerConnectionObserver *>(
                            peerid, peer_connection_observer));
        }

        // Ask to create offer.
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions rtc_options;
        rtc_options.offer_to_receive_video = 0;
        rtc_options.offer_to_receive_audio = 0;
        std::promise<const webrtc::SessionDescriptionInterface *> promise;
        peer_connection->CreateOffer(CreateSessionDescriptionObserver::Create(
                                             peer_connection, promise),
                                     rtc_options);

        // Waiting for offer.
        std::future<const webrtc::SessionDescriptionInterface *> future =
                promise.get_future();
        if (future.wait_for(std::chrono::milliseconds(5000)) ==
            std::future_status::ready) {
            // Answer with the created offer.
            const webrtc::SessionDescriptionInterface *desc = future.get();
            if (desc) {
                std::string sdp;
                desc->ToString(&sdp);

                offer[k_session_description_type_name] = desc->type();
                offer[k_session_description_sdp_name] = sdp;
            } else {
                RTC_LOG(LERROR) << "Failed to create offer";
            }
        } else {
            RTC_LOG(LERROR) << "Failed to create offer";
        }
    }
    return offer;
}

// Auto-answer to a call.
const Json::Value PeerConnectionManager::Call(const std::string &peerid,
                                              const std::string &window_uid,
                                              const std::string &options,
                                              const Json::Value &jmessage) {
    RTC_LOG(INFO) << __FUNCTION__ << " video:" << window_uid
                  << " options:" << options;

    Json::Value answer;

    std::string type;
    std::string sdp;

    if (!rtc::GetStringFromJsonObject(jmessage, k_session_description_type_name,
                                      &type) ||
        !rtc::GetStringFromJsonObject(jmessage, k_session_description_sdp_name,
                                      &sdp)) {
        RTC_LOG(WARNING) << "Can't parse received message.";
    } else {
        PeerConnectionObserver *peer_connection_observer =
                this->CreatePeerConnection(peerid);
        if (!peer_connection_observer) {
            RTC_LOG(LERROR) << "Failed to initialize PeerConnectionObserver";
        } else if (!peer_connection_observer->GetPeerConnection().get()) {
            RTC_LOG(LERROR) << "Failed to initialize PeerConnection";
            delete peer_connection_observer;
        } else {
            rtc::scoped_refptr<webrtc::PeerConnectionInterface>
                    peer_connection =
                            peer_connection_observer->GetPeerConnection();
            RTC_LOG(INFO) << "nbStreams local:"
                          << peer_connection->local_streams()->count()
                          << " remote:"
                          << peer_connection->remote_streams()->count()
                          << " localDescription:"
                          << peer_connection->local_description();

            // register peerid
            {
                std::lock_guard<std::mutex> peerlock(
                        peer_id_to_connection_mutex_);
                peer_id_to_connection_.insert(
                        std::pair<std::string, PeerConnectionObserver *>(
                                peerid, peer_connection_observer));
            }

            // set remote offer
            webrtc::SessionDescriptionInterface *session_description(
                    webrtc::CreateSessionDescription(type, sdp, nullptr));
            if (!session_description) {
                RTC_LOG(WARNING)
                        << "Can't parse received session description message.";
            } else {
                std::promise<const webrtc::SessionDescriptionInterface *>
                        remote_promise;
                peer_connection->SetRemoteDescription(
                        SetSessionDescriptionObserver::Create(peer_connection,
                                                              remote_promise),
                        session_description);
                // waiting for remote description
                std::future<const webrtc::SessionDescriptionInterface *>
                        remote_future = remote_promise.get_future();
                if (remote_future.wait_for(std::chrono::milliseconds(5000)) ==
                    std::future_status::ready) {
                    RTC_LOG(INFO) << "remote_description is ready";
                } else {
                    RTC_LOG(WARNING) << "remote_description is nullptr";
                }
            }

            // add local stream
            if (!this->AddStreams(peer_connection, window_uid, options)) {
                RTC_LOG(WARNING) << "Can't add stream";
            }

            // create answer
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions rtc_options;
            std::promise<const webrtc::SessionDescriptionInterface *>
                    local_promise;
            peer_connection->CreateAnswer(
                    CreateSessionDescriptionObserver::Create(peer_connection,
                                                             local_promise),
                    rtc_options);

            // waiting for answer
            std::future<const webrtc::SessionDescriptionInterface *>
                    local_future = local_promise.get_future();
            if (local_future.wait_for(std::chrono::milliseconds(5000)) ==
                std::future_status::ready) {
                // answer with the created answer
                const webrtc::SessionDescriptionInterface *desc =
                        local_future.get();
                if (desc) {
                    std::string sdp;
                    desc->ToString(&sdp);

                    answer[k_session_description_type_name] = desc->type();
                    answer[k_session_description_sdp_name] = sdp;
                } else {
                    RTC_LOG(LERROR) << "Failed to create answer";
                }
            } else {
                RTC_LOG(LERROR) << "Failed to create answer";
            }

            // TODO: this is not the place to send SendInitFrames. Call
            // SendInitFrames after the video stream is fully established. Send
            //
            // window_uid is window_uid.
            webrtc_server_->SendInitFrames(window_uid);
        }
    }
    return answer;
}

bool PeerConnectionManager::WindowStillUsed(const std::string &window_uid) {
    bool still_used = false;
    for (auto it : peer_id_to_connection_) {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection =
                it.second->GetPeerConnection();
        rtc::scoped_refptr<webrtc::StreamCollectionInterface> local_streams(
                peer_connection->local_streams());
        for (unsigned int i = 0; i < local_streams->count(); i++) {
            if (local_streams->at(i)->id() == window_uid) {
                still_used = true;
                break;
            }
        }
    }
    return still_used;
}

// Hangup a call.
const Json::Value PeerConnectionManager::HangUp(const std::string &peerid) {
    bool result = false;
    RTC_LOG(INFO) << __FUNCTION__ << " " << peerid;

    PeerConnectionObserver *pc_observer = nullptr;
    {
        std::lock_guard<std::mutex> peerlock(peer_id_to_connection_mutex_);
        std::map<std::string, PeerConnectionObserver *>::iterator it =
                peer_id_to_connection_.find(peerid);
        if (it != peer_id_to_connection_.end()) {
            pc_observer = it->second;
            RTC_LOG(LS_ERROR) << "Remove PeerConnection peerid:" << peerid;
            peer_id_to_connection_.erase(it);
        }

        if (pc_observer) {
            rtc::scoped_refptr<webrtc::PeerConnectionInterface>
                    peer_connection = pc_observer->GetPeerConnection();

            rtc::scoped_refptr<webrtc::StreamCollectionInterface> local_streams(
                    peer_connection->local_streams());
            for (unsigned int i = 0; i < local_streams->count(); i++) {
                auto stream = local_streams->at(i);

                std::string window_uid = stream->id();
                bool still_used = this->WindowStillUsed(window_uid);
                if (!still_used) {
                    RTC_LOG(LS_ERROR)
                            << "hangUp stream is no more used " << window_uid;
                    std::lock_guard<std::mutex> mlock(
                            window_uid_to_track_source_mutex_);
                    auto it = window_uid_to_track_source_.find(window_uid);
                    if (it != window_uid_to_track_source_.end()) {
                        window_uid_to_track_source_.erase(it);
                    }

                    RTC_LOG(LS_ERROR) << "hangUp stream closed " << window_uid;
                }

                peer_connection->RemoveStream(stream);
            }

            delete pc_observer;
            result = true;
        }
    }
    Json::Value answer;
    if (result) {
        answer = result;
    }
    RTC_LOG(INFO) << __FUNCTION__ << " " << peerid << " result:" << result;
    return answer;
}

// Get list ICE candidate associated with a PeerConnection.
const Json::Value PeerConnectionManager::GetIceCandidateList(
        const std::string &peerid) {
    RTC_LOG(INFO) << __FUNCTION__;

    Json::Value value;
    std::lock_guard<std::mutex> peerlock(peer_id_to_connection_mutex_);
    std::map<std::string, PeerConnectionObserver *>::iterator it =
            peer_id_to_connection_.find(peerid);
    if (it != peer_id_to_connection_.end()) {
        PeerConnectionObserver *obs = it->second;
        if (obs) {
            value = obs->GetIceCandidateList();
        } else {
            RTC_LOG(LS_ERROR) << "No observer for peer:" << peerid;
        }
    }
    return value;
}

// Check if factory is initialized.
bool PeerConnectionManager::InitializePeerConnection() {
    return (peer_connection_factory_.get() != nullptr);
}

// Create a new PeerConnection.
PeerConnectionManager::PeerConnectionObserver *
PeerConnectionManager::CreatePeerConnection(const std::string &peerid) {
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    for (auto ice_server : ice_server_list_) {
        webrtc::PeerConnectionInterface::IceServer server;
        IceServer srv = GetIceServerFromUrl(ice_server);
        server.uri = srv.url;
        server.username = srv.user;
        server.password = srv.pass;
        config.servers.push_back(server);
    }

    // Use example From
    // https://soru.site/questions/51578447/api-c-webrtcyi-kullanarak-peerconnection-ve-ucretsiz-baglant-noktasn-serbest-nasl
    int minPort = 0;
    int maxPort = 65535;
    std::istringstream is(webrtc_port_range_);
    std::string port;
    if (std::getline(is, port, ':')) {
        minPort = std::stoi(port);
        if (std::getline(is, port, ':')) {
            maxPort = std::stoi(port);
        }
    }
    std::unique_ptr<cricket::PortAllocator> port_allocator(
            new cricket::BasicPortAllocator(new rtc::BasicNetworkManager()));
    port_allocator->SetPortRange(minPort, maxPort);
    RTC_LOG(INFO) << __FUNCTION__
                  << "CreatePeerConnection webrtcPortRange:" << minPort << ":"
                  << maxPort;

    RTC_LOG(INFO) << __FUNCTION__ << "CreatePeerConnection peerid:" << peerid;
    PeerConnectionObserver *obs =
            new PeerConnectionObserver(this->webrtc_server_, this, peerid,
                                       config, std::move(port_allocator));
    if (!obs) {
        RTC_LOG(LERROR) << __FUNCTION__ << "CreatePeerConnection failed";
    }
    return obs;
}

// Get the capturer from its URL.
rtc::scoped_refptr<BitmapTrackSourceInterface>
PeerConnectionManager::CreateVideoSource(
        const std::string &window_uid,
        const std::map<std::string, std::string> &opts) {
    RTC_LOG(INFO) << "window_uid:" << window_uid;

    std::string video = window_uid;
    if (config_.isMember(video)) {
        video = config_[video]["video"].asString();
    }

    return ImageTrackSource::Create(video, opts);
}

// Add a stream to a PeerConnection.
bool PeerConnectionManager::AddStreams(
        webrtc::PeerConnectionInterface *peer_connection,
        const std::string &window_uid,
        const std::string &options) {
    bool ret = false;

    // Compute options.
    // Example options: "rtptransport=tcp&timeout=60"
    std::string optstring = options;
    if (config_.isMember(window_uid)) {
        std::string urlopts = config_[window_uid]["options"].asString();
        if (options.empty()) {
            optstring = urlopts;
        } else if (options.find_first_of("&") == 0) {
            optstring = urlopts + options;
        } else {
            optstring = options;
        }
    }

    // Convert options string into map.
    std::istringstream is(optstring);
    std::map<std::string, std::string> opts;
    std::string key, value;
    while (std::getline(std::getline(is, key, '='), value, '&')) {
        opts[key] = value;
    }

    std::string video = window_uid;
    if (config_.isMember(video)) {
        video = config_[video]["video"].asString();
    }

    // Set bandwidth.
    if (opts.find("bitrate") != opts.end()) {
        int bitrate = std::stoi(opts.at("bitrate"));

        webrtc::BitrateSettings bitrate_param;
        bitrate_param.min_bitrate_bps = absl::optional<int>(bitrate / 2);
        bitrate_param.start_bitrate_bps = absl::optional<int>(bitrate);
        bitrate_param.max_bitrate_bps = absl::optional<int>(bitrate * 2);
        peer_connection->SetBitrate(bitrate_param);

        RTC_LOG(WARNING) << "set bitrate:" << bitrate;
    }

    bool existing_stream = false;
    {
        std::lock_guard<std::mutex> mlock(window_uid_to_track_source_mutex_);
        existing_stream = (window_uid_to_track_source_.find(window_uid) !=
                           window_uid_to_track_source_.end());
    }

    if (!existing_stream) {
        // Create a new stream and add to window_uid_to_track_source_;
        rtc::scoped_refptr<BitmapTrackSourceInterface> video_source(
                this->CreateVideoSource(video, opts));
        RTC_LOG(INFO) << "Adding Stream to map";
        std::lock_guard<std::mutex> mlock(window_uid_to_track_source_mutex_);
        window_uid_to_track_source_[window_uid] = video_source;
    }

    // AddTrack and AddStream to peer_connection
    {
        std::lock_guard<std::mutex> mlock(window_uid_to_track_source_mutex_);
        auto it = window_uid_to_track_source_.find(window_uid);
        if (it != window_uid_to_track_source_.end()) {
            rtc::scoped_refptr<webrtc::MediaStreamInterface> stream =
                    peer_connection_factory_->CreateLocalMediaStream(
                            window_uid);
            if (!stream.get()) {
                RTC_LOG(LS_ERROR) << "Cannot create stream";
            } else {
                rtc::scoped_refptr<BitmapTrackSourceInterface> video_source =
                        it->second;
                rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;
                if (!video_source) {
                    RTC_LOG(LS_ERROR)
                            << "Cannot create capturer video:" << window_uid;
                } else {
                    rtc::scoped_refptr<BitmapTrackSourceInterface> videoScaled =
                            VideoFilter<VideoScaler>::Create(video_source,
                                                             opts);
                    video_track = peer_connection_factory_->CreateVideoTrack(
                            window_uid + "_video", videoScaled);
                }

                if ((video_track) && (!stream->AddTrack(video_track))) {
                    RTC_LOG(LS_ERROR)
                            << "Adding VideoTrack to MediaStream failed";
                }

                if (!peer_connection->AddStream(stream)) {
                    RTC_LOG(LS_ERROR)
                            << "Adding stream to PeerConnection failed";
                } else {
                    RTC_LOG(INFO) << "stream added to PeerConnection";
                    ret = true;
                }
            }
        } else {
            RTC_LOG(LS_ERROR) << "Cannot find stream";
        }
    }

    return ret;
}

// ICE callback.
void PeerConnectionManager::PeerConnectionObserver::OnIceCandidate(
        const webrtc::IceCandidateInterface *candidate) {
    RTC_LOG(INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();

    std::string sdp;
    if (!candidate->ToString(&sdp)) {
        RTC_LOG(LS_ERROR) << "Failed to serialize candidate";
    } else {
        RTC_LOG(INFO) << sdp;

        Json::Value jmessage;
        jmessage[k_candidate_sdp_mid_name] = candidate->sdp_mid();
        jmessage[k_candidate_sdp_mline_index_name] =
                candidate->sdp_mline_index();
        jmessage[k_candidate_sdp_name] = sdp;
        ice_candidate_list_.append(jmessage);
    }
}

rtc::scoped_refptr<BitmapTrackSourceInterface>
PeerConnectionManager::GetVideoTrackSource(const std::string &window_uid) {
    {
        std::lock_guard<std::mutex> mlock(window_uid_to_track_source_mutex_);
        if (window_uid_to_track_source_.find(window_uid) ==
            window_uid_to_track_source_.end()) {
            return nullptr;
        } else {
            return window_uid_to_track_source_.at(window_uid);
        }
    }
}

}  // namespace webrtc_server
}  // namespace visualization
}  // namespace open3d
