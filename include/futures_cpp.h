
#ifndef INCLUDE_FUTURES_CPP_H_
#define INCLUDE_FUTURES_CPP_H_

#ifdef _MSC_VER
#ifdef __cplusplus
#ifdef FUTURES_CPP_EXPORTS
#define FUTURES_CPP_API  extern "C" __declspec(dllexport)
#else
#define FUTURES_CPP_API extern "C" __declspec(dllimport)
#endif
#else
#ifdef FUTURES_CPP_EXPORTS
#define FUTURES_CPP_API __declspec(dllexport)
#else
#define FUTURES_CPP_API __declspec(dllimport)
#endif
#endif
#else /* _MSC_VER */
#ifdef __cplusplus
#ifdef FUTURES_CPP_EXPORTS
#define FUTURES_CPP_API  extern "C" __attribute__((visibility ("default")))
#else
#define FUTURES_CPP_API extern "C"
#endif
#else
#ifdef FUTURES_CPP_EXPORTS
#define FUTURES_CPP_API __attribute__((visibility ("default")))
#else
#define FUTURES_CPP_API
#endif
#endif
#endif

// TODO(cppbuild): Add your library interface here
FUTURES_CPP_API int add_one(int x);

#endif  // INCLUDE_FUTURES_CPP_H_
