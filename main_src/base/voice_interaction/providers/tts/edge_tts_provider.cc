// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/tts/edge_tts_provider.h"
#include "common/crypto_utils.h"
#include "common/mp3_decoder.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace prosophor {

namespace {

constexpr const char* kEdgeHost = "speech.platform.bing.com";
constexpr const char* kEdgePath = "/consumer/speech/synthesize/readaloud/edge/v1";
constexpr const char* kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
constexpr const char* kGecVersion = "1-143.0.3650.75";
constexpr int kOutputChannels = 1;

std::string GenerateGecToken() {
    double ticks = SystemClock::GetCurrentEpochSeconds();
    ticks += 11644473600.0;
    ticks -= std::fmod(ticks, 300.0);
    uint64_t filetime = static_cast<uint64_t>(ticks * 10000000.0);
    return Sha256Hex(std::to_string(filetime) + kTrustedClientToken);
}

std::string BuildSsml(const std::string& text, const std::string& voice) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':  escaped += "&amp;";  break;
            case '<':  escaped += "&lt;";   break;
            case '>':  escaped += "&gt;";   break;
            case '"':  escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default:   escaped += c;        break;
        }
    }
    return "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
           "<voice name='" + voice + "'>"
           "<prosody pitch='+0Hz' rate='+0%' volume='+0%'>"
           + escaped +
           "</prosody></voice></speak>";
}

std::string BuildSpeechConfig(const std::string& date) {
    return "X-Timestamp:" + date + "\r\n"
           "Content-Type:application/json; charset=utf-8\r\n"
           "Path:speech.config\r\n\r\n"
           R"({"context":{"synthesis":{"audio":{"metadataoptions":{)"
           R"("sentenceBoundaryEnabled":"false","wordBoundaryEnabled":"true"},)"
           R"("outputFormat":"audio-24khz-48kbitrate-mono-mp3"}}}})";
}

std::string BuildSsmlMessage(const std::string& ssml, const std::string& request_id,
                              const std::string& date) {
    return "X-RequestId:" + request_id + "\r\n"
           "Content-Type:application/ssml+xml\r\n"
           "X-Timestamp:" + date + "Z\r\n"
           "Path:ssml\r\n\r\n"
           + ssml;
}

std::vector<uint8_t> ExtractAudioPayload(const uint8_t* data, size_t len) {
    if (len < 2) return {};
    uint16_t header_len = static_cast<uint16_t>(data[0]) << 8 | data[1];
    if (static_cast<size_t>(header_len) + 2 > len) {
        LOG_DEBUG("EdgeTts: skipping non-audio binary frame (header_len={}, frame={})", header_len, len);
        return {};
    }
    const uint8_t* payload_start = data + 2 + header_len;
    if (payload_start + 4 > data + len) return {};
    const uint8_t* body = payload_start;
    if (len >= 4 && body[0] == '\r' && body[1] == '\n') body += 2;
    if (len >= 4 && body[0] == '\r' && body[1] == '\n') body += 2;
    size_t body_len = (data + len) - body;
    if (body_len == 0) return {};
    return {body, body + body_len};
}

}  // namespace

// ---- Constructor / Destructor ----

EdgeTtsProvider::EdgeTtsProvider() = default;
EdgeTtsProvider::~EdgeTtsProvider() = default;

// ---- WebSocket connection ----

bool EdgeTtsProvider::ConnectAndExchange(const std::string& text, const std::string& voice) {
    std::string gec = GenerateGecToken();
    std::string conn_id = GenerateUuid();
    std::string url = "wss://" + std::string(kEdgeHost) + kEdgePath
        + "?TrustedClientToken=" + kTrustedClientToken
        + "&Sec-MS-GEC=" + gec
        + "&Sec-MS-GEC-Version=" + kGecVersion
        + "&ConnectionId=" + conn_id;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0");
    headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br, zstd");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    headers = curl_slist_append(headers, "Pragma: no-cache");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    headers = curl_slist_append(headers,
        "Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");
    headers = curl_slist_append(headers, "Sec-WebSocket-Version: 13");
    std::string muid = GenerateUuid();
    std::transform(muid.begin(), muid.end(), muid.begin(), ::toupper);
    std::string muid_header = "Cookie: muid=" + muid + ";";
    headers = curl_slist_append(headers, muid_header.c_str());

    bool ok = ws_.Connect(url, headers);
    if (headers) curl_slist_free_all(headers);
    if (!ok) {
        LOG_ERROR("EdgeTts: connect failed: {}", ws_.last_error());
        return false;
    }

    std::string date_str = SystemClock::FormatGmtString();
    std::string config_msg = BuildSpeechConfig(date_str);
    if (!ws_.SendText(config_msg)) {
        LOG_ERROR("EdgeTts: failed to send speech.config: {}", ws_.last_error());
        ws_.Close();
        return false;
    }
    LOG_DEBUG("EdgeTts: speech.config sent ({} bytes)", config_msg.size());

    std::string ssml = BuildSsml(text, voice);
    std::string req_id = GenerateUuid();
    std::string ssml_msg = BuildSsmlMessage(ssml, req_id, date_str);
    if (!ws_.SendText(ssml_msg)) {
        LOG_ERROR("EdgeTts: failed to send SSML: {}", ws_.last_error());
        ws_.Close();
        return false;
    }
    LOG_INFO("EdgeTts: connected and exchanged ({} chars text, {} B SSML msg)", text.size(), ssml_msg.size());
    return true;
}

// ---- Receive all MP3 audio frames, extract payloads, accumulate ----

std::vector<uint8_t> EdgeTtsProvider::ReceiveAllMp3() {
    std::vector<uint8_t> mp3;

    while (true) {
        std::vector<uint8_t> buf;
        int flags = 0;
        CURLcode res = ws_.Recv(buf, &flags, 30000);
        if (res != CURLE_OK) {
            LOG_WARN("EdgeTts: recv error: {}", curl_easy_strerror(res));
            break;
        }
        if (buf.empty()) continue;

        if (flags & CURLWS_BINARY) {
            auto payload = ExtractAudioPayload(buf.data(), buf.size());
            if (!payload.empty()) {
                mp3.insert(mp3.end(), payload.begin(), payload.end());
            }
            continue;
        }
        if (flags & CURLWS_TEXT) {
            buf.push_back(0);
            LOG_DEBUG("EdgeTts: text frame: {}", reinterpret_cast<const char*>(buf.data()));
            if (std::strstr(reinterpret_cast<const char*>(buf.data()), "turn.end")) break;
            continue;
        }
        if (flags & CURLWS_CLOSE) {
            LOG_DEBUG("EdgeTts: WebSocket close frame");
            break;
        }
    }

    return mp3;
}

// ---- Synthesize (WebSocket → MP3 → PCM via dr_mp3) ----

TtsResponse EdgeTtsProvider::Synthesize(const TtsRequest& request) {
    if (!ConnectAndExchange(request.text, request.voice)) {
        TtsResponse r;
        r.error_msg = "EdgeTTS: failed to connect to WebSocket";
        return r;
    }

    auto mp3_data = ReceiveAllMp3();
    if (mp3_data.empty()) {
        ws_.Close();
        TtsResponse r;
        r.error_msg = "EdgeTTS: no audio data received";
        return r;
    }
    ws_.Close();

    LOG_INFO("EdgeTts: received {} bytes MP3 data", mp3_data.size());

    Mp3Decoder mp3;
    if (!mp3.Init(mp3_data.data(), mp3_data.size())) {
        TtsResponse r;
        r.error_msg = "EdgeTTS: MP3 decode failed";
        return r;
    }

    if (mp3.GetPcmFrameCount() == 0) {
        mp3.Uninit();
        TtsResponse r;
        r.error_msg = "EdgeTTS: no PCM frames in decoded audio";
        return r;
    }

    std::vector<int16_t> pcm;
    mp3.ReadAllS16(pcm);
    int sample_rate = mp3.SampleRate();
    mp3.Uninit();

    TtsResponse resp;
    resp.success = true;
    resp.pcm = std::move(pcm);
    resp.sample_rate = sample_rate;
    resp.channels = kOutputChannels;
    return resp;
}

}  // namespace prosophor
