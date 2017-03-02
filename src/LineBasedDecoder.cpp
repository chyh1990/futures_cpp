#include <futures/codec/LineBasedDecoder.h>
#include <futures/core/Cursor.h>

namespace futures {
namespace codec {

using folly::io::Cursor;
using folly::IOBufQueue;

LineBasedDecoder::LineBasedDecoder(uint32_t maxLength,
                                             bool stripDelimiter,
                                             TerminatorType terminatorType)
    : maxLength_(maxLength)
    , stripDelimiter_(stripDelimiter)
    , terminatorType_(terminatorType) {}

Optional<LineBasedOut> LineBasedDecoder::decode(folly::IOBufQueue &buf)
{
  int64_t eol = findEndOfLine(buf);

  if (!discarding_) {
    if (eol >= 0) {
      Cursor c(buf.front());
      c += eol;
      auto delimLength = c.read<char>() == '\r' ? 2 : 1;
      if (eol > maxLength_) {
        buf.split(eol + delimLength);
        throw IOError("line too long");
      }

      std::unique_ptr<folly::IOBuf> frame;

      if (stripDelimiter_) {
        frame = buf.split(eol);
        buf.trimStart(delimLength);
      } else {
        frame = buf.split(eol + delimLength);
      }

      return Optional<Out>(std::move(frame));
    } else {
      auto len = buf.chainLength();
      if (len > maxLength_) {
        discardedBytes_ = len;
        buf.trimStart(len);
        discarding_ = true;
        throw IOError("line too long");
      }
      return Optional<Out>(folly::none);
    }
  } else {
    if (eol >= 0) {
      Cursor c(buf.front());
      c += eol;
      auto delimLength = c.read<char>() == '\r' ? 2 : 1;
      buf.trimStart(eol + delimLength);
      discardedBytes_ = 0;
      discarding_ = false;
    } else {
      discardedBytes_ = buf.chainLength();
      buf.move();
    }

    return Optional<Out>(folly::none);
  }
}

int64_t LineBasedDecoder::findEndOfLine(IOBufQueue& buf) {
  Cursor c(buf.front());
  for (uint32_t i = 0; i < maxLength_ && i < buf.chainLength(); i++) {
    auto b = c.read<char>();
    if (b == '\n' && terminatorType_ != TerminatorType::CARRIAGENEWLINE) {
      return i;
    } else if (terminatorType_ != TerminatorType::NEWLINE &&
               b == '\r' && !c.isAtEnd() && c.read<char>() == '\n') {
      return i;
    }
  }

  return -1;
}

}
}
