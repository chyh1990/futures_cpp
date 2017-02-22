#include <gtest/gtest.h>
#include <futures/Core.h>
#include <futures/io/StreamAdapter.h>

using namespace futures;

static folly::IOBufQueue genBuffer() {
    folly::IOBufQueue q;
    q.append(folly::IOBuf::copyBuffer("TESTBUFFER1\n", 12));
    q.append(folly::IOBuf::copyBuffer("TESTBUFFER2\n42", 12 + 2));
    return q;
}

TEST(StreamAdapter, Read) {
    auto q = genBuffer();
    IOBufStreambuf buf(&q);
    std::istream is(&buf);

    std::string s1, s2;
    int i3;
    is >> s1 >> s2 >> i3;
    EXPECT_EQ(s1, "TESTBUFFER1");
    EXPECT_EQ(s2, "TESTBUFFER2");
    EXPECT_EQ(i3, 42);

    EXPECT_TRUE(is.eof());
    EXPECT_FALSE(is >> i3);
}

TEST(StreamAdapter, Empty) {
    folly::IOBufQueue q;
    IOBufStreambuf buf(&q);
    std::istream is(&buf);
    int i;
    EXPECT_FALSE(is >> i);
    EXPECT_FALSE(is >> i);
}

TEST(StreamAdapter, Write) {
    folly::IOBufQueue q;
    IOBufStreambuf buf(&q);
    std::ostream os(&buf);
    os << "TEST1\n";
    os << "TEST2\n";
    os.flush();

    EXPECT_EQ(q.front()->computeChainDataLength(), 12);
}

TEST(StreamAdapter, WriteMore) {
    folly::IOBufQueue q;
    IOBufStreambuf buf(&q);
    std::ostream os(&buf);
    for (int i = 0; i < 10000; ++i) {
        os << "TEST42\n";
    }
    os.flush();

    EXPECT_EQ(q.front()->computeChainDataLength(), 7 * 10000);
}

TEST(StreamAdapter, WriteEndl) {
    folly::IOBufQueue q;
    IOBufStreambuf buf(&q);
    std::ostream os(&buf);
    os << "HELLO" << std::endl;
    os.flush();

    EXPECT_EQ(q.front()->computeChainDataLength(), 6);
}

TEST(StreamAdapter, Copy) {
    auto q = genBuffer();
    IOBufStreambuf buf(&q);
    std::istream is(&buf);

    std::ostringstream oss;
    oss << is.rdbuf();
}
