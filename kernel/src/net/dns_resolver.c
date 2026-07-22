#include <timeros/net/dns.h>

#include <timeros/net/net_exec.h>
#include <timeros/net/socket.h>

typedef struct {
    netif_t *netif;
    ipaddr_t server;
    u16 port;
    dns_query_t query;
    int handle;
    net_err_t result;
} dns_request_t;

static u16 dns_next_id;

static void dns_open(void *arg)
{
    dns_request_t *request = arg;
    request->handle = net_socket_open(NET_SOCKET_UDP);
    request->result = request->handle < 0 ?
                      (net_err_t)request->handle : NET_ERR_OK;
}

static void dns_send(void *arg)
{
    dns_request_t *request = arg;
    request->result = net_socket_sendto(request->handle, request->netif,
                                        &request->server, request->port,
                                        request->query.bytes,
                                        request->query.length);
}

static void dns_close(void *arg)
{
    dns_request_t *request = arg;
    request->result = net_socket_close(request->handle);
}

static net_err_t dns_submit(net_exec_proc_t proc, dns_request_t *request)
{
    net_err_t error = net_exec_submit(proc, request, 0);
    return error < 0 ? error : request->result;
}

net_err_t dns_resolve_a(netif_t *netif, const ipaddr_t *server,
                        u16 port, const char *name, ipaddr_t *address,
                        int timeout_ms)
{
    if (netif == 0 || server == 0 || port == 0 || name == 0 ||
        address == 0 || timeout_ms < 0)
        return NET_ERR_PARAM;

    dns_request_t request = {
        .netif = netif,
        .server = *server,
        .port = port,
        .handle = -1,
    };
    u16 id = __atomic_fetch_add(&dns_next_id, 1, __ATOMIC_RELAXED);
    net_err_t error = dns_query_encode(&request.query, id, name);
    if (error < 0)
        return error;

    error = dns_submit(dns_open, &request);
    if (error < 0)
        return error;
    error = dns_submit(dns_send, &request);
    if (error < 0)
        goto close;

    u8 response[512];
    ipaddr_t source;
    u16 source_port = 0;
    int received = net_socket_recvfrom(request.handle, response,
                                       (int)sizeof(response), &source,
                                       &source_port, timeout_ms);
    if (received < 0) {
        error = (net_err_t)received;
        goto close;
    }
    if (!ipaddr_is_equal(&source, server) || source_port != port) {
        error = NET_ERR_UNREACH;
        goto close;
    }
    error = dns_response_parse(response, received, id, name, address);

close:
    {
        net_err_t close_error = dns_submit(dns_close, &request);
        if (error == NET_ERR_OK && close_error < 0)
            error = close_error;
    }
    return error;
}
