// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

// voice_pipeline 实现 —— 设计与线程约定见 voice_pipeline.h。
#include "voice_interaction/voice_pipeline.h"

#include <algorithm>
#include <utility>

#include "common/log_wrapper.h"
#include "vad/energy_vad.h"

namespace prosophor::voice {

namespace {

int FramePeak(const int16_t* pcm, size_t samples) {
    int peak = 0;
    for (size_t i = 0; i < samples; ++i) {
        const int v = pcm[i] < 0 ? -static_cast<int>(pcm[i]) : pcm[i];
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

}  // namespace

VoiceTuning BuildVoiceTuning(const VoiceConfig& cfg) {
    constexpr int frame_ms = 30;
    VoiceTuning tuning;
    tuning.vad_rms_dbfs = cfg.vad_rms_dbfs;
    tuning.start_confirm_frames = cfg.start_confirm_frames;
    tuning.end_silence_frames =
        cfg.end_silence_ms / frame_ms > 1 ? cfg.end_silence_ms / frame_ms : 1;
    tuning.max_utterance_frames =
        cfg.max_utterance_ms / frame_ms > 1 ? cfg.max_utterance_ms / frame_ms : 1;
    tuning.pre_roll_frames = cfg.pre_roll_frames;
    tuning.max_pending_speech = cfg.max_pending_speech;
    tuning.max_pending_asr_jobs = cfg.max_pending_asr_jobs;
    return tuning;
}

VoicePipeline::VoicePipeline(TranscribeFn transcribe, SpeakFn speak, VoiceTuning tuning,
                           bool start_workers, std::unique_ptr<IVad> vad)
    : transcribe_fn_(std::move(transcribe)),
      speak_fn_(std::move(speak)),
      tuning_(tuning),
      vad_(std::move(vad)),
      asr_queue_(tuning.max_pending_asr_jobs > 0 ? tuning.max_pending_asr_jobs : 8),
      text_queue_(tuning.max_pending_texts > 0 ? tuning.max_pending_texts : 1),
      speech_queue_(tuning.max_pending_speech > 0 ? tuning.max_pending_speech : 1) {
    if (!vad_) {
        vad_ = std::make_unique<EnergyVad>(tuning_.vad_rms_dbfs);
    }
    if (start_workers) {
        Start();
    }
}

VoicePipeline::~VoicePipeline() {
    Stop();
}

void VoicePipeline::Start() {
    if (running_.exchange(true)) {
        return;
    }
    asr_thread_ = std::thread([this] { AsrWorker(); });
    tts_thread_ = std::thread([this] { TtsWorker(); });
    LOG_INFO("VoicePipeline: workers 已启动");
}

void VoicePipeline::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    asr_queue_.Close();
    speech_queue_.Close();
    text_queue_.Close();
    if (asr_thread_.joinable()) {
        asr_thread_.join();
    }
    if (tts_thread_.joinable()) {
        tts_thread_.join();
    }
    LOG_INFO("VoicePipeline: workers 已停止");
}

void VoicePipeline::ResetVadState() {
    in_speech_ = false;
    start_confirm_ = 0;
    silence_frames_ = 0;
    interim_flush_counter_ = 0;
    utterance_.clear();
    pre_roll_.clear();
    if (vad_) {
        vad_->Reset();
    }
}

void VoicePipeline::MaybeFlushInterim(bool voiced) {
    if (!tuning_.emit_interim) {
        return;
    }
    ++interim_flush_counter_;
    const bool voiced_trigger =
        voiced && interim_flush_counter_ >= tuning_.interim_voiced_frames;
    const bool silence_trigger =
        !voiced && silence_frames_ <= tuning_.interim_max_silence_frames &&
        interim_flush_counter_ >= tuning_.interim_silence_flush_frames;
    if (!(voiced_trigger || silence_trigger) || asr_in_flight_.load() != 0) {
        return;
    }
    interim_flush_counter_ = 0;
    std::vector<int16_t> interim = utterance_;
    BumpCounter(&Stats::interim_requests);
    EnqueueAsrJob(AsrJobKind::kInterim, std::move(interim), current_seq_);
}

void VoicePipeline::EnqueueAsrJob(AsrJobKind kind, std::vector<int16_t> pcm, uint64_t seq) {
    if (!asr_queue_.TryPush(AsrJob{kind, std::move(pcm), seq})) {
        BumpCounter(&Stats::asr_queue_dropped);   // 满队列拒绝新句
        return;
    }
    asr_in_flight_.fetch_add(1);
}

void VoicePipeline::FeedFrame(const int16_t* pcm, size_t samples) {
    if (pcm == nullptr || samples < static_cast<size_t>(tuning_.frame_samples)) {
        return;
    }
    if (reset_requested_.exchange(false)) {
        ResetVadState();
    }
    BumpCounter(&Stats::feed_frames);

    const size_t frame = static_cast<size_t>(tuning_.frame_samples);
    const bool voiced = vad_ != nullptr && vad_->ProcessFrame(pcm, frame);

    if (!in_speech_) {
        // 空闲：维护回捞环，连续确认后进入说话态（回捞帧一并入句，防首字被切）
        pre_roll_.emplace_back(pcm, pcm + frame);
        const size_t keep = static_cast<size_t>(std::max(0, tuning_.pre_roll_frames));
        while (pre_roll_.size() > keep) {
            pre_roll_.pop_front();
        }
        start_confirm_ = voiced ? start_confirm_ + 1 : 0;
        if (start_confirm_ >= tuning_.start_confirm_frames) {
            in_speech_ = true;
            silence_frames_ = 0;
            interim_flush_counter_ = 0;
            current_seq_ = ++next_seq_;
            utterance_.clear();
            for (const auto& buffered : pre_roll_) {
                utterance_.insert(utterance_.end(), buffered.begin(), buffered.end());
            }
            pre_roll_.clear();
            start_confirm_ = 0;
        }
        return;
    }

    // 说话中：累积并判定收句（静音够长或到达单句上限）
    utterance_.insert(utterance_.end(), pcm, pcm + frame);
    silence_frames_ = voiced ? 0 : silence_frames_ + 1;
    MaybeFlushInterim(voiced);

    const bool too_long = tuning_.max_utterance_frames > 0 &&
        static_cast<int>(utterance_.size() / frame) >= tuning_.max_utterance_frames;
    if (!too_long && silence_frames_ < tuning_.end_silence_frames) {
        return;
    }

    std::vector<int16_t> formed = std::move(utterance_);
    ResetVadState();

    // 三道闸：最短句长 / 峰值自检（数字静音坑）/ 单飞（上一句在飞则丢弃）
    const int frames = static_cast<int>(formed.size() / frame);
    if (tuning_.min_utterance_frames > 0 && frames < tuning_.min_utterance_frames) {
        BumpCounter(&Stats::dropped_short);
        return;
    }
    if (tuning_.min_peak > 0 &&
        FramePeak(formed.data(), formed.size()) < tuning_.min_peak) {
        BumpCounter(&Stats::dropped_lowpeak);
        return;
    }

    if (tuning_.drop_utterance_if_asr_busy && asr_in_flight_.load() > 0) {
        BumpCounter(&Stats::dropped_busy);
        return;
    }

    BumpCounter(&Stats::utterances);
    EnqueueAsrJob(AsrJobKind::kFinal, std::move(formed), current_seq_);
}

std::optional<std::string> VoicePipeline::PollAsrResult(int timeout_ms) {
    std::string text;
    if (!text_queue_.Pop(text, timeout_ms)) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> VoicePipeline::PollAsrLatest() {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!has_latest_result_) {
        return std::nullopt;
    }
    has_latest_result_ = false;
    return std::move(latest_result_);
}

void VoicePipeline::EnqueueSpeech(SpeechRequest request) {
    if (request.text.empty()) {
        return;
    }
    speech_queue_.Push(std::move(request));   // 满队列丢最旧
}

void VoicePipeline::PublishResult(const std::string& text, uint64_t seq, bool is_final) {
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        // interim 不得覆盖同句 final；final 不覆盖更新一句的 interim
        if (is_final ? (seq >= last_published_seq_) : (seq > last_published_seq_)) {
            latest_result_ = text;
            has_latest_result_ = true;
            last_published_seq_ = seq;
        } else {
            stale = !is_final;
        }
    }
    if (stale) {
        BumpCounter(&Stats::stale_interim_dropped);
    }
}

void VoicePipeline::AsrWorker() {
    AsrJob job;
    while (asr_queue_.Pop(job)) {
        std::string text;
        try {
            if (transcribe_fn_) {
                text = transcribe_fn_(job.pcm);
            }
        } catch (...) {
            text.clear();   // 异常不出 worker（按空结果处理）
        }
        asr_in_flight_.fetch_sub(1);

        if (text.empty()) {
            BumpCounter(&Stats::asr_empty);
            continue;
        }
        if (job.kind != AsrJobKind::kInterim) {
            PublishResult(text, job.seq, /*is_final=*/true);
            text_queue_.Push(std::move(text));
        } else {
            BumpCounter(&Stats::interim_results);
            PublishResult(text, job.seq, /*is_final=*/false);
        }
    }
}

void VoicePipeline::TtsWorker() {
    SpeechRequest request;
    while (speech_queue_.Pop(request)) {
        speaking_.store(true);
        bool ok = false;
        if (speak_fn_) {
            try {
                ok = speak_fn_(request);
            } catch (...) {
                ok = false;   // 异常不出 worker
            }
        }
        speaking_.store(false);
        if (ok) {
            BumpCounter(&Stats::spoken);
        }
    }
}

void VoicePipeline::BumpCounter(int Stats::*field) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ++(stats_.*field);
}

VoicePipeline::Stats VoicePipeline::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace prosophor::voice
