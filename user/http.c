#include <stddef.h>
#include <timeros/net/net_err.h>

static int http_equal(const char *left, int length, const char *right)
{
    int index;
    for (index = 0; index < length; index++) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static int http_find(const char *data, int length, const char *needle,
                     int needle_length, int start)
{
    int index;
    for (index = start; index + needle_length <= length; index++) {
        if (http_equal(data + index, needle_length, needle))
            return index;
    }
    return -1;
}

int http_response_validate(const char *data, int length,
                           const char *expected_body, int expected_length)
{
    static const char status[] = "HTTP/1.0 200 OK\r\n";
    static const char header[] = "Content-Length:";
    int header_end;
    int line_start;
    int content_length = -1;
    int body_start;
    int header_count = 0;

    if (data == 0 || expected_body == 0 || length < (int)sizeof(status) - 1)
        return NET_ERR_PARAM;
    if (!http_equal(data, (int)sizeof(status) - 1, status))
        return NET_ERR_FORMAT;
    header_end = http_find(data, length, "\r\n\r\n", 4,
                           (int)sizeof(status) - 1);
    if (header_end < 0)
        return NET_ERR_SIZE;

    line_start = (int)sizeof(status) - 1;
    while (line_start < header_end) {
        int line_end = http_find(data, header_end + 2, "\r\n", 2,
                                 line_start);
        int value_start;
        int value_end;
        int value = 0;
        if (line_end < 0)
            return NET_ERR_FORMAT;
        if (line_end == line_start) {
            line_start += 2;
            continue;
        }
        if (line_end - line_start <= (int)sizeof(header) - 1 ||
            !http_equal(data + line_start, (int)sizeof(header) - 1,
                        header)) {
            line_start = line_end + 2;
            continue;
        }
        header_count++;
        if (header_count != 1)
            return NET_ERR_FORMAT;
        value_start = line_start + (int)sizeof(header) - 1;
        while (value_start < line_end && data[value_start] == ' ')
            value_start++;
        value_end = line_end;
        if (value_start == value_end)
            return NET_ERR_FORMAT;
        while (value_start < value_end) {
            if (data[value_start] < '0' || data[value_start] > '9')
                return NET_ERR_FORMAT;
            value = value * 10 + data[value_start] - '0';
            if (value < 0)
                return NET_ERR_SIZE;
            value_start++;
        }
        content_length = value;
        line_start = line_end + 2;
    }
    if (content_length < 0 || content_length != expected_length)
        return NET_ERR_FORMAT;
    body_start = header_end + 4;
    if (length - body_start != content_length)
        return NET_ERR_SIZE;
    if (!http_equal(data + body_start, content_length, expected_body))
        return NET_ERR_FORMAT;
    return NET_ERR_OK;
}
