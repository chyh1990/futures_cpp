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
            : q_(q) {
                assert(q);
                char *p = static_cast<char*>(q_->writableTail());
                Base::setp(p, p + q_->tailroom());
            }

    private:
        int_type overflow(int_type ch) override {
            // FUTURES_DLOG(INFO) << "overflow " << ch;
            sync();
            if (ch == std::char_traits<CharT>::eof()) {
                return ch;
            }
            auto p = q_->preallocate(2000, 4000);
            char *pc = static_cast<char*>(p.first);
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
            }
            return 0;
        }

        folly::IOBufQueue *q_;
};

using IOBufStreambuf = BasicIOBufStreambuf<char>;

}
