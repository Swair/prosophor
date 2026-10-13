// CameraSource 实现 —— ffmpeg 单帧抓拍 + 原子发布（详见头注释）
#include "camera_source.h"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <vector>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "platform/platform.h"

namespace prosophor::nr::infra {

namespace {

// 帧文件名：frame_NNNNNN.jpg / frame_NNNNNN.tmp.jpg（序号 6 位左补零，字典序即时间序）
std::string SeqName(uint64_t seq, bool tmp) {
    std::string digits = std::to_string(seq);
    std::string name = "frame_";
    if (digits.size() < 6) name.append(6 - digits.size(), '0');
    name += digits;
    if (tmp) name += ".tmp";
    name += ".jpg";
    return name;
}

// 可用 v4l2 节点清单（抓帧失败的报错里用；板上 video0/1 是元数据节点，
// 只有其中一个能做 QUERYCAP，列出来省得逐个试）
std::string ListVideoDevices() {
    std::string out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("video", 0) != 0) continue;
        if (!out.empty()) out += " ";
        out += "/dev/" + name;
    }
    return out.empty() ? "（未发现 /dev/video*）" : out;
}

// 目录清理：删掉 dir 下所有 frame_* 文件（含上次运行残留的 .tmp）
void CleanStaleFrames(const std::string& dir) {
    for (const auto& name : ListDir(dir, ".jpg")) {
        if (name.rfind("frame_", 0) == 0) {
            RemoveFile(JoinPath(dir, name));
        }
    }
}

}  // namespace

CameraSource::~CameraSource() { Stop(); }

std::string CameraSource::PathOf(uint64_t seq, bool tmp) const {
    return JoinPath(config_.output_dir, SeqName(seq, tmp));
}

bool CameraSource::CaptureOne(uint64_t seq, std::string& error) {
    const std::string tmp_path = PathOf(seq, /*tmp=*/true);
    const std::string final_path = PathOf(seq, /*tmp=*/false);
    const std::string cmd =
        "ffmpeg -hide_banner -loglevel error -f v4l2 -i " +
        Platform::ShellEscape(config_.device) + " -frames:v 1 -y " +
        Platform::ShellEscape(tmp_path);

    const int timeout_sec =
        config_.timeout_ms > 0 ? (config_.timeout_ms + 999) / 1000 : 0;
    const CommandOutput result = Platform::RunCommandWithOutput(cmd, timeout_sec);
    if (result.exit_code != 0) {
        error = "ffmpeg 退出码 " + std::to_string(result.exit_code) +
                (result.output.empty() ? "" : "：" + result.output);
        RemoveFile(tmp_path);   // 半截/超时残留清掉，不留下一个会误导的文件
        return false;
    }
    if (!FileExists(tmp_path)) {
        error = "ffmpeg 报成功但未产出文件";
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);   // 原子发布
    if (ec) {
        error = "发布 rename 失败：" + ec.message();
        RemoveFile(tmp_path);
        return false;
    }
    return true;
}

void CameraSource::Publish(uint64_t seq) {
    frame_count_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        latest_path_ = PathOf(seq, /*tmp=*/false);
    }
    published_seqs_.push_back(seq);
    const int keep = config_.keep_frames > 0 ? config_.keep_frames : 1;
    while (static_cast<int>(published_seqs_.size()) > keep) {
        RemoveFile(PathOf(published_seqs_.front(), /*tmp=*/false));
        published_seqs_.pop_front();
    }
}

bool CameraSource::Start(const CameraConfig& config, std::string& error) {
    if (running_.load()) {
        error = "CameraSource 已在运行";
        return false;
    }
    config_ = config;
    if (!EnsureDirectory(config_.output_dir)) {
        error = "无法创建帧目录 " + config_.output_dir;
        return false;
    }
    CleanStaleFrames(config_.output_dir);

    // 同步抓第一帧：设备错在这一步就报出来，主线程据此降级（语音问答照常）
    if (!CaptureOne(0, error)) {
        error = "摄像头 " + config_.device + " 抓帧失败：" + error +
                "\n  可用节点：" + ListVideoDevices() +
                "（-v4l2 探一下：ffmpeg -f v4l2 -i /dev/videoN -frames:v 1 -y /tmp/probe.jpg）";
        return false;
    }
    Publish(0);
    next_seq_ = 1;

    running_.store(true);
    thread_ = std::thread([this] { CaptureLoop(); });
    return true;
}

void CameraSource::CaptureLoop() {
    using namespace std::chrono;
    int consecutive_failures = 0;
    auto next_capture_at = steady_clock::now() + milliseconds(config_.interval_ms);

    while (running_.load()) {
        // 节拍等待：抓拍耗时不叠加到周期上（下一拍 = 上一拍 + interval；
        // 抓拍慢于节拍时自然追拍，不补偿 —— 单帧抓拍 ~1.2s，正常不会追）
        {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_until(lock, next_capture_at,
                                [this] { return !running_.load(); });
            if (!running_.load()) break;
        }

        const uint64_t seq = next_seq_++;
        std::string error;
        if (!CaptureOne(seq, error)) {
            ++consecutive_failures;
            // 退出路径上在飞的 ffmpeg 会被打断（退出码 255）：不是故障，不报
            if (running_.load() &&
                (consecutive_failures == 1 || consecutive_failures % 20 == 0)) {
                LOG_ERROR("nutribot: 摄像头抓帧失败（连续 {} 次）：{}",
                          consecutive_failures, error.c_str());
            }
        } else {
            if (consecutive_failures > 0) {
                LOG_INFO("nutribot: 摄像头抓帧恢复（连续失败 {} 次后）",
                         consecutive_failures);
                consecutive_failures = 0;
            }
            Publish(seq);
            LOG_DEBUG("nutribot: 抓帧 {} 已发布（累计 {} 帧）",
                      PathOf(seq, false).c_str(), frame_count_.load());
        }

        next_capture_at += milliseconds(config_.interval_ms);
        if (next_capture_at < steady_clock::now()) {
            next_capture_at = steady_clock::now();   // 追拍到当前，不让节拍点无限落后
        }
    }
}

void CameraSource::Stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();   // Start 失败后可能留下空壳
        return;
    }
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string CameraSource::LatestFrame() const {
    std::lock_guard<std::mutex> lock(path_mutex_);
    return latest_path_;
}

}  // namespace prosophor::nr::infra
