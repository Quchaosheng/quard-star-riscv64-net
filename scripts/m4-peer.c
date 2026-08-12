#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { kEtherType = 0x88b5, kMinFrame = 60, kMaxFrame = 1514 };

static const uint8_t kGuestMac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint8_t kHostMac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};
static const uint8_t kBroadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static uint16_t read_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static void write_be16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)(value >> 8);
  p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}

static uint32_t payload_checksum(const uint8_t *payload, size_t length) {
  uint32_t sum = 0;
  for (size_t i = 0; i < length; ++i) sum += payload[i];
  return sum;
}

static int decode_request(const uint8_t *frame, size_t frame_length,
                          uint32_t *sequence, const uint8_t **payload,
                          size_t *payload_length) {
  if (frame_length < 24) return -1;
  if (memcmp(frame, kBroadcast, 6) != 0 ||
      memcmp(frame + 6, kGuestMac, 6) != 0 || read_be16(frame + 12) != kEtherType)
    return -1;

  *sequence = read_be32(frame + 14);
  *payload_length = read_be16(frame + 18);
  const size_t checksum_offset = 20 + *payload_length;
  if (checksum_offset + 4 > frame_length) return -1;
  *payload = frame + 20;
  if (read_be32(frame + checksum_offset) !=
      payload_checksum(*payload, *payload_length))
    return -1;
  for (size_t i = 0; i < *payload_length; ++i) {
    if ((*payload)[i] != (uint8_t)(*sequence ^ (uint32_t)i ^ 0x5aU)) return -1;
  }
  return 0;
}

static size_t encode_response(uint8_t *frame, size_t capacity, uint32_t sequence,
                              const uint8_t *payload, size_t payload_length) {
  const size_t used = 24 + payload_length;
  const size_t frame_length = used < kMinFrame ? kMinFrame : used;
  if (payload_length > UINT16_MAX || frame_length > capacity) return 0;
  memset(frame, 0, frame_length);
  memcpy(frame, kGuestMac, 6);
  memcpy(frame + 6, kHostMac, 6);
  write_be16(frame + 12, kEtherType);
  write_be32(frame + 14, sequence);
  write_be16(frame + 18, (uint16_t)payload_length);
  memcpy(frame + 20, payload, payload_length);
  write_be32(frame + 20 + payload_length,
             payload_checksum(payload, payload_length));
  return frame_length;
}

static double monotonic_seconds(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0.0;
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static int write_text(const char *path, const char *text) {
  if (path == NULL) return 0;
  FILE *file = fopen(path, "w");
  if (file == NULL) return -1;
  const int failed = fputs(text, file) < 0 || fclose(file) != 0;
  return failed ? -1 : 0;
}

static int write_stats(const char *path, unsigned frames, double elapsed) {
  if (path == NULL) return 0;
  char json[128];
  const int length = snprintf(json, sizeof(json),
                              "{\"frames\":%u,\"elapsed_seconds\":%.9f}\n",
                              frames, elapsed);
  return length > 0 && (size_t)length < sizeof(json) ? write_text(path, json) : -1;
}

static int open_peer(const char *interface) {
  const unsigned index = if_nametoindex(interface);
  if (index == 0) return -1;
  const int fd = socket(AF_PACKET, SOCK_RAW, htons(kEtherType));
  if (fd < 0) return -1;
  struct sockaddr_ll address = {0};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(kEtherType);
  address.sll_ifindex = (int)index;
  if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int run_peer(const char *interface, unsigned count, double timeout,
                    const char *ready_file, const char *stats_file) {
  const double started = monotonic_seconds();
  const double deadline = started + timeout;
  int fd = open_peer(interface);
  if (fd < 0) return -1;
  if (write_text(ready_file, "ready\n") != 0) {
    close(fd);
    return -1;
  }

  unsigned received = 0;
  uint32_t expected = 0;
  while (received < count) {
    const double remaining = deadline - monotonic_seconds();
    if (remaining <= 0.0) {
      fprintf(stderr, "m4-peer: timed out after %u/%u frames\n", received, count);
      close(fd);
      return -1;
    }
    struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
    int wait_ms = (int)(remaining * 1000.0);
    if (wait_ms < 1) wait_ms = 1;
    const int polled = poll(&poll_fd, 1, wait_ms);
    if (polled == 0) continue;
    if (polled < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1;
    }

    uint8_t request[kMaxFrame];
    struct sockaddr_ll source = {0};
    socklen_t source_length = sizeof(source);
    const ssize_t length = recvfrom(fd, request, sizeof(request), 0,
                                    (struct sockaddr *)&source, &source_length);
    if (length < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1;
    }
    if (source.sll_pkttype == PACKET_OUTGOING) continue;

    uint32_t sequence;
    const uint8_t *payload;
    size_t payload_length;
    if (decode_request(request, (size_t)length, &sequence, &payload,
                       &payload_length) != 0 || sequence != expected) {
      fprintf(stderr, "m4-peer: invalid frame or sequence\n");
      close(fd);
      return -1;
    }
    uint8_t response[kMaxFrame];
    const size_t response_length = encode_response(
        response, sizeof(response), sequence, payload, payload_length);
    if (response_length == 0 || send(fd, response, response_length, 0) !=
                                    (ssize_t)response_length) {
      close(fd);
      return -1;
    }
    ++received;
    ++expected;
  }
  close(fd);
  return write_stats(stats_file, received, monotonic_seconds() - started);
}

static int self_test(void) {
  uint8_t payload[32];
  const uint32_t sequence = 7;
  for (size_t i = 0; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(sequence ^ (uint32_t)i ^ 0x5aU);

  uint8_t response[kMinFrame];
  if (encode_response(response, sizeof(response), sequence, payload,
                      sizeof(payload)) != kMinFrame)
    return 1;
  if (memcmp(response, kGuestMac, 6) != 0 || memcmp(response + 6, kHostMac, 6) != 0)
    return 1;

  uint8_t request[kMinFrame] = {0};
  memcpy(request, kBroadcast, 6);
  memcpy(request + 6, kGuestMac, 6);
  write_be16(request + 12, kEtherType);
  write_be32(request + 14, sequence);
  write_be16(request + 18, sizeof(payload));
  memcpy(request + 20, payload, sizeof(payload));
  write_be32(request + 52, payload_checksum(payload, sizeof(payload)));
  uint32_t decoded_sequence;
  const uint8_t *decoded_payload;
  size_t decoded_length;
  if (decode_request(request, sizeof(request), &decoded_sequence,
                     &decoded_payload, &decoded_length) != 0 ||
      decoded_sequence != sequence || decoded_length != sizeof(payload) ||
      memcmp(decoded_payload, payload, sizeof(payload)) != 0)
    return 1;
  request[20] ^= 1;
  return decode_request(request, sizeof(request), &decoded_sequence,
                        &decoded_payload, &decoded_length) == 0;
}

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s INTERFACE --count N [--timeout S] [--ready-file PATH] "
          "[--stats-file PATH]\n",
          program);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return self_test();
  if (argc == 2 && strcmp(argv[1], "--probe-raw") == 0) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(kEtherType));
    if (fd < 0) return 1;
    close(fd);
    return 0;
  }
  if (argc < 4) {
    usage(argv[0]);
    return 2;
  }
  const char *interface = argv[1];
  const char *ready_file = NULL;
  const char *stats_file = NULL;
  unsigned count = 0;
  double timeout = 30.0;
  for (int i = 2; i < argc; ++i) {
    if (i + 1 >= argc) {
      usage(argv[0]);
      return 2;
    }
    const char *value = argv[++i];
    if (strcmp(argv[i - 1], "--count") == 0) count = (unsigned)strtoul(value, NULL, 10);
    else if (strcmp(argv[i - 1], "--timeout") == 0) timeout = strtod(value, NULL);
    else if (strcmp(argv[i - 1], "--ready-file") == 0) ready_file = value;
    else if (strcmp(argv[i - 1], "--stats-file") == 0) stats_file = value;
    else {
      usage(argv[0]);
      return 2;
    }
  }
  if (count == 0 || timeout <= 0.0) {
    usage(argv[0]);
    return 2;
  }
  if (run_peer(interface, count, timeout, ready_file, stats_file) != 0) {
    fprintf(stderr, "m4-peer: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}
