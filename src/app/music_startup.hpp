#pragma once

namespace fasttris::app {

class MusicStartupState {
public:
    void markFirstFramePresented() { first_frame_presented_ = true; }
    void setLifecycleSuspended(bool suspended) { lifecycle_suspended_ = suspended; }

    bool claimStart() {
        if (!first_frame_presented_ || lifecycle_suspended_ || starting_ || ready_ || failed_) return false;
        starting_ = true;
        return true;
    }

    void finish(bool success) {
        if (!starting_) return;
        starting_ = false;
        ready_ = success;
        failed_ = !success;
    }

    bool starting() const { return starting_; }
    bool ready() const { return ready_; }
    bool failed() const { return failed_; }

private:
    bool first_frame_presented_{};
    bool lifecycle_suspended_{};
    bool starting_{};
    bool ready_{};
    bool failed_{};
};

} // namespace fasttris::app
