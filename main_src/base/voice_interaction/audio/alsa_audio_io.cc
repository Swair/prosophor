// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

// ALSA 音频 I/O 实现 —— 设计与线程约定见 alsa_audio_io.h。
#include "audio/alsa_audio_io.h"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "common/log_wrapper.h"

namespace prosophor::voice {

namespace {

// 播放分块：Write 每写一块检查一次 Abort —— 退出路径最多多等一块的时长
constexpr size_t playback_chunk_samples = 9600;   // 0.2s @48k

constexpr int poll_fd_capacity = 8;

std::string AlsaError(int err) {
    return snd_strerror(err);
}

}  // namespace

// ============================ AlsaAudioInput ============================

AlsaAudioInput::AlsaAudioInput(CaptureConfig cfg) : cfg_(std::move(cfg)) {}

AlsaAudioInput::~AlsaAudioInput() {
    Close();
}

bool AlsaAudioInput::Open(std::string& error) {
    Close();

    // 非阻塞 + poll 超时：读端不会长期阻塞，采集线程得以及时看到退出标志
    int err = snd_pcm_open(&pcm_, cfg_.device.c_str(), SND_PCM_STREAM_CAPTURE,
                           SND_PCM_NONBLOCK);
    if (err < 0) {
        pcm_ = nullptr;
        error = "打开采集设备失败 " + cfg_.device + ": " + AlsaError(err);
        return false;
    }
    err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, cfg_.channels,
                             static_cast<unsigned>(cfg_.sample_rate),
                             1 /*允许软重采样*/, cfg_.latency_us);
    if (err < 0) {
        error = "设置采集参数失败 " + cfg_.device + ": " + AlsaError(err);
        Close();
        return false;
    }

    frame_samples_ = cfg_.frame_samples;
    muted_.store(false);
    LOG_INFO("AlsaAudioInput: 已打开 {} ({}Hz {}ch, {} 样本/帧)", cfg_.device.c_str(),
             cfg_.sample_rate, cfg_.channels, cfg_.frame_samples);
    return true;
}

int AlsaAudioInput::Read(int16_t* out, int max_samples, int timeout_ms) {
    if (pcm_ == nullptr || out == nullptr || max_samples < frame_samples_) {
        return -1;
    }

    // 非阻塞模式下 PREPARED 状态的流不会自动开流：readi 只回 -EAGAIN、poll 永不
    // 就绪（实测帧=0 的根因）。首次读取与欠载 prepare 之后都需要显式启动。
    if (snd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED) {
        snd_pcm_start(pcm_);
    }

    pollfd fds[poll_fd_capacity];
    const int nfds = snd_pcm_poll_descriptors(pcm_, fds, poll_fd_capacity);
    if (nfds <= 0) {
        // 无 poll 描述符（罕见）：退化为定时等待，调用方仍能轮询退出标志
        usleep(static_cast<useconds_t>(timeout_ms) * 1000);
        return 0;
    }
    const int ready = poll(fds, static_cast<nfds_t>(nfds), timeout_ms);
    if (ready <= 0) {
        return 0;   // 超时
    }

    // 一次排空"当前可用"的数据（上限 max_samples）：板上负载抖动时固定读一帧
    // 会让设备环形缓冲持续溢出（实测每几秒一次，丢音频、句子被截断）
    const snd_pcm_sframes_t avail = snd_pcm_avail(pcm_);
    if (avail <= 0) {
        return 0;
    }
    const snd_pcm_uframes_t want = static_cast<snd_pcm_uframes_t>(
        std::min<snd_pcm_sframes_t>(avail, max_samples));

    const snd_pcm_sframes_t got = snd_pcm_readi(pcm_, out, want);
    if (got == -EAGAIN) {
        return 0;
    }
    if (got == -EPIPE) {
        snd_pcm_prepare(pcm_);   // 溢出恢复
        LOG_WARN("AlsaAudioInput: 缓冲溢出，已 prepare 恢复");
        return 0;
    }
    if (got < 0) {
        LOG_ERROR("AlsaAudioInput: readi 错误: {}", AlsaError(static_cast<int>(got)));
        return -1;
    }

    if (muted_.load()) {
        return 0;   // 播报期丢帧（流已排空，不 stop）
    }
    return static_cast<int>(got);
}

void AlsaAudioInput::Close() {
    if (pcm_ == nullptr) {
        return;
    }
    snd_pcm_drop(pcm_);
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

// ============================ AlsaAudioOutput ============================

AlsaAudioOutput::AlsaAudioOutput(PlaybackConfig cfg) : cfg_(std::move(cfg)) {}

AlsaAudioOutput::~AlsaAudioOutput() {
    Close();
}

bool AlsaAudioOutput::Open(int sample_rate, int channels, std::string& error) {
    Close();

    cfg_.sample_rate = sample_rate;
    cfg_.channels = channels;
    int err = snd_pcm_open(&pcm_, cfg_.device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        pcm_ = nullptr;
        error = "打开播放设备失败 " + cfg_.device + ": " + AlsaError(err);
        return false;
    }
    err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, cfg_.channels,
                             static_cast<unsigned>(cfg_.sample_rate),
                             1 /*允许软重采样*/, cfg_.latency_us);
    if (err < 0) {
        error = "设置播放参数失败 " + cfg_.device + ": " + AlsaError(err);
        Close();
        return false;
    }

    abort_.store(false);
    LOG_INFO("AlsaAudioOutput: 已打开 {} ({}Hz {}ch)", cfg_.device.c_str(),
             cfg_.sample_rate, cfg_.channels);
    return true;
}

bool AlsaAudioOutput::Write(const int16_t* data, size_t samples) {
    if (pcm_ == nullptr || (data == nullptr && samples > 0)) {
        return false;
    }
    size_t written = 0;
    int badfd_recoveries = 0;
    while (written < samples) {
        if (abort_.load()) {
            return false;
        }
        const size_t chunk = std::min(playback_chunk_samples, samples - written);
        const snd_pcm_sframes_t ret = snd_pcm_writei(pcm_, data + written, chunk);
        if (ret == -EPIPE) {
            snd_pcm_prepare(pcm_);   // 欠载恢复
            LOG_WARN("AlsaAudioOutput: 欠载，已 prepare 恢复");
            continue;
        }
        if (ret == -EAGAIN) {
            snd_pcm_wait(pcm_, 100);
            continue;
        }
        if (ret == -EBADFD) {
            // 流不在可写状态（drain/abort 之后处于 SETUP）：prepare 拉回 PREPARED
            // 再写。实测症状：第二轮播报起全部 writei 失败（只播得出第一轮）
            const int prep = snd_pcm_prepare(pcm_);
            if (prep < 0 || ++badfd_recoveries > 3) {
                LOG_ERROR("AlsaAudioOutput: writei 状态错误（-EBADFD），恢复失败");
                return false;
            }
            continue;
        }
        if (ret < 0) {
            LOG_ERROR("AlsaAudioOutput: writei 错误: {}", AlsaError(static_cast<int>(ret)));
            return false;
        }
        written += static_cast<size_t>(ret);
    }
    return true;
}

bool AlsaAudioOutput::Drain() {
    if (pcm_ == nullptr) {
        return false;
    }
    if (abort_.load()) {
        return false;
    }
    const int err = snd_pcm_drain(pcm_);
    if (err < 0 && err != -EAGAIN) {
        LOG_WARN("AlsaAudioOutput: drain 返回 {}", AlsaError(err));
    }
    // drain 完成后流落在 SETUP 态：不 prepare 回去，下一轮 writei 会 -EBADFD
    // （实测第二轮播报全失败的根因）。prepare 拉回 PREPARED，供下次直接写。
    snd_pcm_prepare(pcm_);
    return true;
}

void AlsaAudioOutput::Close() {
    if (pcm_ == nullptr) {
        return;
    }
    snd_pcm_drop(pcm_);
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

}  // namespace prosophor::voice
