// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SPO_HPP
#define SPO_HPP
#include "spo.h"
#include <utility>
namespace spo {
class session {
    spo_result result_;
    bool owns_;

  public:
    explicit session(const spo_config &config) noexcept
        : result_(spo_init(&config)), owns_(result_ == SPO_OK) {}
    ~session() {
        if (owns_)
            spo_shutdown();
    }
    session(const session &) = delete;
    session &operator=(const session &) = delete;
    session(session &&other) noexcept : result_(other.result_), owns_(other.owns_) {
        other.owns_ = false;
    }
    session &operator=(session &&) = delete;
    spo_result result() const noexcept {
        return result_;
    }
};
} // namespace spo
#endif
