#pragma once

#include <futures/codec/Codec.h>

namespace futures {
namespace codec {

using LineBasedOut = std::unique_ptr<folly::IOBuf>;

class LineBasedDecoder : public codec::DecoderBase<LineBasedDecoder, LineBasedOut>
{
public:
  using Out = LineBasedOut;

  enum class TerminatorType {
      BOTH,
      NEWLINE,
      CARRIAGENEWLINE
  };

  explicit LineBasedDecoder(
          uint32_t maxLength = UINT_MAX,
          bool stripDelimiter = true,
          TerminatorType terminatorType = TerminatorType::BOTH);

  Try<Optional<Out>> decode(folly::IOBufQueue &buf);

private:

  int64_t findEndOfLine(folly::IOBufQueue& buf);

  uint32_t maxLength_;
  bool stripDelimiter_;

  bool discarding_{false};
  uint32_t discardedBytes_{0};

  TerminatorType terminatorType_;
};

}
}
