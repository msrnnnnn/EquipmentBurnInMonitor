/* config.h - Windows/MinGW configuration for libmodbus */

#ifndef CONFIG_H
#define CONFIG_H
#define VERSION "3.2.0"
#define PACKAGE_STRING "libmodbus 3.2.0"

#define HAVE_WINSOCK2_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRERROR 1
#define STDC_HEADERS 1
#define HAVE_SELECT 1
#define HAVE_SOCKET 1
#define HAVE_GETADDRINFO 1
#define HAVE_GAI_STRERROR 1
#define HAVE_INET_PTON 1

#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#endif
