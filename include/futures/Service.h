#pragma once

#include <futures/Future.h>
#include <futures/EventExecutor.h>

namespace futures {

template <typename Req, typename Resp = Req>
class Service {
public:
    virtual BoxedFuture<Resp> operator()(Req req) = 0;

    virtual BoxedFuture<folly::Unit> close() {
        return makeOk().boxed();
    }

    virtual bool isAvailable() {
        return true;
    }

    virtual ~Service() = default;
};

template <typename ReqA, typename RespA,
          typename ReqB = ReqA, typename RespB = RespA>
class ServiceFilter : public Service<ReqA, RespA> {
public:
  explicit ServiceFilter(std::shared_ptr<Service<ReqB, RespB>> service)
      : service_(service) {}
  virtual ~ServiceFilter() = default;

  virtual BoxedFuture<folly::Unit> close() override {
    return service_->close();
  }

  virtual bool isAvailable() override {
    return service_->isAvailable();
  }

protected:
  std::shared_ptr<Service<ReqB, RespB>> service_;
};

#if 0
template <typename Req, typename Resp = Req>
class SerialServerDispatcher : public HandlerAdapter<Req, Resp> {
public:
  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  explicit SerialServerDispatcher(Service<Req, Resp>* service)
      : service_(service) {}

  void read(Context* ctx, Req in) override {
    auto f = (*service_)(std::move(in))
      .andThen([this] (Resp rep) {
        return this->getContext()->fireWrite(std::move(rep));
    });
    EventExecutor::current()->spawn(std::move(f));
  }

private:
  Service<Req, Resp>* service_;
};
#endif

}
