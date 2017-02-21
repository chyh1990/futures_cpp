#pragma once

#include <map>
#include <array>
#include <boost/regex.hpp>
#include <futures/Service.h>
#include <futures/http/HttpCodec.h>

namespace futures {
namespace http {

#define REGEX_NS boost

class HttpRequest {
public:
  http::Request raw;
  REGEX_NS::smatch matches;

  HttpRequest(http::Request&& r)
    : raw(std::move(r)) {}

};

class HttpController : public Service<Request, Response> {
public:
  enum class HttpMethod : int {
    Delete,
    Get,
    Head,
    Post,
    Put,
    Unknown,
  };
  const static size_t kHttpMethodCount = (size_t)HttpMethod::Unknown;

  using RequestHandler = std::function<BoxedFuture<Response>(HttpRequest)>;

  HttpController() {
      default_[kHttpMethodCount] = defaultHandler;
  }

  void addRoute(HttpMethod method, const std::string &pattern, RequestHandler handler) {
    resource_[pattern][(size_t)method] = handler;
  }

  void addDefaultRoute(HttpMethod method, RequestHandler handler) {
    default_[(size_t)method] = handler;
  }

  void post(const std::string &pattern, RequestHandler handler) {
    addRoute(HttpMethod::Post, pattern, handler);
  }

  void get(const std::string &pattern, RequestHandler handler) {
    addRoute(HttpMethod::Get, pattern, handler);
  }

  void head(const std::string &pattern, RequestHandler handler) {
    addRoute(HttpMethod::Head, pattern, handler);
  }

  void put(const std::string &pattern, RequestHandler handler) {
    addRoute(HttpMethod::Put, pattern, handler);
  }

  HttpController(const HttpController&) = delete;
  HttpController& operator=(const HttpController&) = delete;

  BoxedFuture<Response> operator()(Request req) {
    HttpRequest r(std::move(req));
    auto func = findResource(&r);
    FUTURES_CHECK(func) << "must not be empty";
    try {
      return func(std::move(r));
    } catch (std::exception &err) {
      FUTURES_LOG(ERROR) << "service error: " << err.what();
      Response resp;
      resp.http_errno = 500;
      resp.body.append("<h1>Internal Error</h1>", 23);
      return makeOk(std::move(resp));
    }
  }
protected:
  static BoxedFuture<Response> defaultHandler(HttpRequest req) {
      Response resp;
      resp.http_errno = 404;
      resp.body.append("<h1>Not Found</h1>", 18);
      return makeOk(std::move(resp)).boxed();
  }

private:
  class regex_orderable : public REGEX_NS::regex {
      std::string str;
      public:
      regex_orderable(const char *regex_cstr) : REGEX_NS::regex(regex_cstr), str(regex_cstr) {}
      regex_orderable(const std::string &regex_str) : REGEX_NS::regex(regex_str), str(regex_str) {}
      bool operator<(const regex_orderable &rhs) const {
          return str<rhs.str;
      }
  };

  std::map<regex_orderable, std::array<RequestHandler, kHttpMethodCount>> resource_;
  std::array<RequestHandler, kHttpMethodCount + 1> default_;

  RequestHandler findResource(HttpRequest *req) {
      auto m = req->raw.method;
      if (m >= kHttpMethodCount) {
          return default_[kHttpMethodCount];
      }
      for(auto &regex_method: resource_) {
        if (regex_method.second[m]) {
            REGEX_NS::smatch sm_res;
            if(REGEX_NS::regex_match(req->raw.path, sm_res, regex_method.first)) {
                req->matches = std::move(sm_res);
                return regex_method.second[m];
            }
        }
      }
      if (default_[m]) {
          return default_[m];
      }
      return default_[kHttpMethodCount];
  }
#undef REGEX_NS

};


}
}
