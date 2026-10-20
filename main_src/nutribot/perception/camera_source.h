// camera_source：常驻抓帧线程（ffmpeg 单帧抓拍 -> 落盘 -> 原子发布）
//
// 为什么不是一条常驻 ffmpeg 管道：发布一帧要原子。ffmpeg 直写目标文件名时，
// VL 侧可能读到半张图；这里每次抓拍先写 frame_NNNNNN.tmp.jpg，ffmpeg 正常
// 退出（-frames:v 1）后再 rename(2) 成 frame_NNNNNN.jpg —— 同文件系统 rename
// 是原子的，VL 只看得见完整帧。
//
// 文件名为什么带序号而不是复用两个名字：VlFoodDetector 的"同帧缓存"键是
// 路径|mtime|size，复用的名字会让"新一帧"命中旧缓存（识别结果停在两轮前）。
//
// 保留最近 keep_frames 帧：VL 推理是秒级，抓帧线程若在读取期间把旧帧删了，
// VL 侧路径会凭空消失；保留窗口 = keep_frames × interval（默认 9s）远大于
// 单次推理耗时。
//
// 线程模型：单抓帧线程 + 条件变量（Stop 立即唤醒，不等下一次节拍）。抓帧失败
// 只记日志不退出线程（摄像头短暂掉线后自动恢复）；LatestFrame() 加锁读取。
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace prosophor::nr::infra {

struct CameraConfig {
    std::string device = "/dev/video2";                          // v4l2 节点
    std::string output_dir = "/tmp/prosophor/nutribot_camera";   // 帧落盘目录
    int interval_ms = 3000;                                      // 发布节拍
    int timeout_ms = 8000;                                       // 单次抓拍超时
    int keep_frames = 3;                                         // 落盘保留帧数
};

class CameraSource {
public:
    CameraSource() = default;
    ~CameraSource();
    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;

    // 起线程前先同步抓一帧做设备校验（设备/输出目录错在这里响亮失败）；
    // 失败返回 false 并给出原因（含可用 /dev/video* 节点清单）
    bool Start(const CameraConfig& config, std::string& error);
    /// 停止并 join 抓帧线程；幂等
    void Stop();

    /// 最新完整帧路径；尚未产出过帧时为空串（线程安全）
    std::string LatestFrame() const;
    /// 已成功发布的帧数
    int frame_count() const { return frame_count_.load(); }

private:
    void CaptureLoop();
    /// 抓一帧（写 .tmp.jpg -> rename 发布名）；成功返回 true
    bool CaptureOne(uint64_t seq, std::string& error);
    /// 发布一帧并滚动删除超保留数的旧帧（仅抓帧线程调用）
    void Publish(uint64_t seq);
    std::string PathOf(uint64_t seq, bool tmp) const;

    CameraConfig config_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> frame_count_{0};
    uint64_t next_seq_ = 0;              // 仅抓帧线程读写
    std::deque<uint64_t> published_seqs_;  // 已发布序号（保留窗口滚动）

    mutable std::mutex path_mutex_;
    std::string latest_path_;            // 已发布的完整帧

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
};

}  // namespace prosophor::nr::infra
