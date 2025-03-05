#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus__
extern "C" {
#endif

// Headers for hton / ntoh
#ifdef _WIN32
#include <Winsock2.h>
#define htobe64(x) htonll(x)
#define htobe32(x) htonl(x)
#define htobe16(x) htons(x)
#define be64toh(x) ntohll(x)
#define be32toh(x) ntohl(x)
#define be16toh(x) ntohs(x)
#elif defined(__linux__)
// NOTE: My guess is that most microcontroller SDKs have some sort of support
// for these headers aswell
// TODO: Try compiling this for ESP
#include <arpa/inet.h>
#include <endian.h>
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <machine/endian.h>
#define htobe64(x) htonll(x)
#define htobe32(x) htonl(x)
#define htobe16(x) htons(x)
#define be64toh(x) ntohll(x)
#define be32toh(x) ntohl(x)
#define be16toh(x) ntohs(x)
#endif

#ifdef __cplusplus__
}
#endif
#endif
