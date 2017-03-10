#pragma once

namespace futures {

template <typename Derive, typename T>
class ThreadLocalData {
public:
    class WithGuard {
    public:
        WithGuard(Derive *c, T* t)
            : c_(c), old_(c->current_) {
            c->current_ = t;
        }

        ~WithGuard() { c_->current_ = old_; }
    private:
        Derive *c_;
        T *old_;
    };

    static Derive* this_thread() {
#if __APPLE__
        static Derive _tls;
#else
        thread_local Derive _tls;
#endif
        return &_tls;
    }

    static T *current() {
        return this_thread()->current_;
    }

    // template <typename F>
    // void set(Task *task, F&& f) {
    //     WithGuard g(this, task);
    //     F();
    // }

protected:
    ThreadLocalData() : current_(nullptr) {}

    T *current_;

};

}
