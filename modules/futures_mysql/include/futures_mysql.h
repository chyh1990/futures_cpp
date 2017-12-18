
#ifndef INCLUDE_FUTURES_MYSQL_H_
#define INCLUDE_FUTURES_MYSQL_H_

#ifdef _MSC_VER
#ifdef __cplusplus
#ifdef FUTURES_MYSQL_EXPORTS
#define FUTURES_MYSQL_API  extern "C" __declspec(dllexport)
#else
#define FUTURES_MYSQL_API extern "C" __declspec(dllimport)
#endif
#else
#ifdef FUTURES_MYSQL_EXPORTS
#define FUTURES_MYSQL_API __declspec(dllexport)
#else
#define FUTURES_MYSQL_API __declspec(dllimport)
#endif
#endif
#else /* _MSC_VER */
#ifdef __cplusplus
#ifdef FUTURES_MYSQL_EXPORTS
#define FUTURES_MYSQL_API  extern "C" __attribute__((visibility ("default")))
#else
#define FUTURES_MYSQL_API extern "C"
#endif
#else
#ifdef FUTURES_MYSQL_EXPORTS
#define FUTURES_MYSQL_API __attribute__((visibility ("default")))
#else
#define FUTURES_MYSQL_API
#endif
#endif
#endif

// TODO(cppbuild): Add your library interface here
FUTURES_MYSQL_API int add_one(int x);

#endif  // INCLUDE_FUTURES_MYSQL_H_
