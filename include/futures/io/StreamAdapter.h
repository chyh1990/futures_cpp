#pragma once

#include <sstream>
#include <futures/Core.h>
#include <futures/core/IOBufQueue.h>

namespace futures {

template <typename CharT = char>
class BasicIOBufStreambuf : public std::basic_streambuf<CharT> {
    public:
        using Base = std::basic_streambuf<CharT>;
        using char_type = typename Base::char_type;
        using int_type = typename Base::int_type;

        BasicIOBufStreambuf(folly::IOBufQueue *q)
            : q_(q), cur_(q->front()) {
                assert(q);
                CharT *p = static_cast<CharT*>(q_->writableTail());
                // assert(p);
                Base::setp(p, p + q_->tailroom());
                if (!cur_) {
                    Base::setg(nullptr, nullptr, nullptr);
                } else {
                    Base::setg((CharT*)cur_->data(), (CharT*)cur_->data(),
                            (CharT*)cur_->tail());
                }
            }

    private:
        int_type overflow(int_type ch) override {
            // FUTURES_DLOG(INFO) << "overflow " << ch;
            sync();
            if (ch == std::char_traits<CharT>::eof()) {
                return ch;
            }
            auto p = q_->preallocate(2000, 4000);
            CharT *pc = static_cast<CharT*>(p.first);
            Base::setp(pc, pc + p.second);
            *Base::pptr() = ch;
            Base::pbump(1);
            return ch;
        }

        int sync() override {
            std::ptrdiff_t n = Base::pptr() - Base::pbase();
            if (n > 0) {
                // FUTURES_DLOG(INFO) << "FLUSHED " << n;
                q_->postallocate(n);
                CharT *p = static_cast<CharT*>(q_->writableTail());
                Base::setp(p, p + q_->tailroom());
            }
            return 0;
        }

        int_type underflow() override {
            if (Base::gptr() < Base::egptr())
                return std::char_traits<CharT>::to_int_type(*Base::gptr());
            if (!cur_)
                return std::char_traits<CharT>::eof();
            auto next = cur_->next();
            if (next == q_->front())
                return std::char_traits<CharT>::eof();
            cur_ = next;
            Base::setg((CharT*)cur_->data(), (CharT*)cur_->data(),
                    (CharT*)cur_->tail());
            return std::char_traits<CharT>::to_int_type(*Base::gptr());
        }

        folly::IOBufQueue *q_;
        const folly::IOBuf *cur_;
};

using IOBufStreambuf = BasicIOBufStreambuf<char>;

}
