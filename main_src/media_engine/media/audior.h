#pragma once

#include <string>
#include <memory>


class Audior {
    public:
        explicit Audior(const std::string& file_path);

        ~Audior();
        Audior(const Audior&) = delete;
        Audior& operator=(const Audior&) = delete;
        Audior(Audior&&) noexcept = default;
        Audior& operator=(Audior&&) noexcept = default;

        // loops: -1=无限循环，0=播放一次，>0=播放次数
        bool Play(int loops = 0);

    private:
        struct AudiorImpl;
        std::unique_ptr<AudiorImpl> impl_{nullptr};
};
