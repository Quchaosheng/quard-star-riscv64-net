#include <assert.h>
#include <string.h>

#include <timeros/net/net_err.h>

int http_response_validate(const char *, int, const char *, int);

static const char response[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Connection: close\r\n"
    "\r\n"
    "m7b-http-body";
static const char body[] = "m7b-http-body";

int main(void)
{
    char split[sizeof(response)];
    const char *duplicate =
        "HTTP/1.0 200 OK\r\nContent-Length: 13\r\n"
        "Content-Length: 13\r\n\r\nm7b-http-body";
    const char *wrong_status =
        "HTTP/1.0 404 Not Found\r\nContent-Length: 13\r\n\r\n"
        "m7b-http-body";
    const char *missing_length =
        "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n"
        "m7b-http-body";
    const char *truncated =
        "HTTP/1.0 200 OK\r\nContent-Length: 13\r\n\r\nm7b-http";

    assert(http_response_validate(response, (int)strlen(response),
                                  body, (int)strlen(body)) == NET_ERR_OK);
    memcpy(split, response, 37);
    memcpy(split + 37, response + 37, sizeof(response) - 37);
    assert(http_response_validate(split, (int)strlen(split), body,
                                  (int)strlen(body)) == NET_ERR_OK);
    assert(http_response_validate(duplicate, (int)strlen(duplicate), body,
                                  (int)strlen(body)) == NET_ERR_FORMAT);
    assert(http_response_validate(wrong_status, (int)strlen(wrong_status),
                                  body, (int)strlen(body)) == NET_ERR_FORMAT);
    assert(http_response_validate(missing_length, (int)strlen(missing_length),
                                  body, (int)strlen(body)) == NET_ERR_FORMAT);
    assert(http_response_validate(truncated, (int)strlen(truncated), body,
                                  (int)strlen(body)) == NET_ERR_SIZE);
    assert(http_response_validate(response, (int)strlen(response),
                                  "wrong-body", 10) == NET_ERR_FORMAT);
    return 0;
}
