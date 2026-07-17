// Retained-MUI value animation primitives.

#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace snt::ui {

class Animation {
public:
    using Setter = std::function<void(float)>;

    Animation(float from, float to, float duration_s, Setter setter);
    bool tick(float dt);
    bool finished() const { return finished_; }

private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    float duration_s_ = 0.0f;
    float elapsed_s_ = 0.0f;
    Setter setter_;
    bool finished_ = false;
};

class Animator {
public:
    void add(Animation animation);
    void update(float dt);
    bool empty() const { return animations_.empty(); }

private:
    std::vector<Animation> animations_;
};

}  // namespace snt::ui
