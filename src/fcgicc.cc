// vim: set expandtab ts=4 sw=4 :
/*
 * Copyright 2008, 2009 Andrey Zholos. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file is part of the FastCGI C++ Class library (fcgicc) version 0.1,
 * available at http://althenia.net/fcgicc
 */


#include "fcgicc.h"

#include <cstring> // bzero, memcpy
#include <stdexcept>

#include <errno.h> // E*
#include <unistd.h> // read, write, close, unlink
#include <arpa/inet.h> // hton*
#include <netinet/in.h> // sockaddr_in, INADDR_*
#include <sys/select.h> // select, fd_set, FD_*, timeval
#include <sys/socket.h> // socket, bind, accept, listen, sockaddr, AF_*, SOCK_*
#include <sys/un.h> // sockaddr_un

#include <fastcgi.h>


FastCGIServer::RequestInfo::RequestInfo() :
    params_closed(false),
    in_closed(false),
    output_closed(false)
{
}



FastCGIServer::Connection::Connection() :
    close_responsibility(false),
    close_socket(false)
{
}


int
FastCGIServer::HandlerBase::operator()(FastCGIRequest&)
{
    return 0;
}


FastCGIServer::FastCGIServer() :
    handle_request(new HandlerBase),
    handle_data(new HandlerBase),
    handle_complete(new HandlerBase)
{
}


FastCGIServer::~FastCGIServer()
{
    for (std::vector<int>::iterator it = listen_sockets.begin();
            it != listen_sockets.end(); ++it)
        close(*it);

    for (std::vector<std::string>::iterator it = listen_unlink.begin();
            it != listen_unlink.end(); ++it)
        unlink(it->c_str());

    for (std::map<int, Connection*>::iterator it = read_sockets.begin();
            it != read_sockets.end(); ++it) {
        close(it->first);
        for (RequestList::iterator req_it = it->second->requests.begin();
                req_it != it->second->requests.end(); ++req_it)
            delete req_it->second;
        delete it->second;
    }
}


void
FastCGIServer::request_handler(int (* function)(FastCGIRequest&))
{
    handle_request.reset(new StaticHandler(function));
}


void
FastCGIServer::data_handler(int (* function)(FastCGIRequest&))
{
    handle_data.reset(new StaticHandler(function));
}


void
FastCGIServer::complete_handler(int (* function)(FastCGIRequest&))
{
    handle_complete.reset(new StaticHandler(function));
}


void
FastCGIServer::set_handler(std::unique_ptr<HandlerBase> &handler, HandlerBase* new_handler)
{
    handler.reset(new_handler);
}


void
FastCGIServer::listen(unsigned tcp_port)
{
    int listen_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_socket == -1)
        throw std::runtime_error("socket() failed");

    try {
        struct sockaddr_in sa;
        bzero(&sa, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(tcp_port);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listen_socket, (struct sockaddr*)&sa, sizeof(sa)) == -1)
            throw std::runtime_error("bind() failed");

        if (::listen(listen_socket, 100))
            throw std::runtime_error("listen() failed");

        listen_sockets.push_back(listen_socket);

    } catch (...) {
        close(listen_socket);
        throw;
    }
}


void
FastCGIServer::listen(const std::string& local_path)
{
    int listen_socket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listen_socket == -1)
        throw std::runtime_error("socket() failed");

    try {
        struct sockaddr_un sa;
        bzero(&sa, sizeof(sa));
        sa.sun_family = AF_LOCAL;

        std::string::size_type size = local_path.size();
        if (size >= sizeof(sa.sun_path))
            throw std::runtime_error("path too long");
        if (local_path.find_first_of('\0') != std::string::npos)
            throw std::runtime_error("null character in path");

        std::memcpy(sa.sun_path, local_path.data(), size);

        unlink(local_path.c_str());
        try {
            if (bind(listen_socket, (struct sockaddr*)&sa,
                    sizeof(sa) - (sizeof(sa.sun_path) - size - 1)) == -1)
                throw std::runtime_error("bind() failed");

            if (::listen(listen_socket, 100))
                throw std::runtime_error("listen() failed");

            listen_sockets.push_back(listen_socket);
            listen_unlink.push_back(local_path);

        } catch (...) {
            unlink(local_path.c_str());
            throw;
        }

    } catch (...) {
        close(listen_socket);
        throw;
    }
}


void
FastCGIServer::abandon_files()
{
    listen_unlink.clear();
}


void
FastCGIServer::process(int timeout_ms)
{
    char buffer[4096];
    fd_set fs_read;
    fd_set fs_write;
    int nfd = 0;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

    FD_ZERO(&fs_read);
    FD_ZERO(&fs_write);

    for (std::vector<int>::const_iterator it = listen_sockets.begin();
            it != listen_sockets.end(); ++it) {
        FD_SET(*it, &fs_read);
        nfd = std::max(nfd, *it);
    }

    for (std::map<int, Connection*>::const_iterator it = read_sockets.begin();
            it != read_sockets.end(); ++it) {
        FD_SET(it->first, &fs_read);
        if (!it->second->output_buffer.empty())
            FD_SET(it->first, &fs_write);
        nfd = std::max(nfd, it->first);
    }

    int select_result = select(nfd + 1, &fs_read, &fs_write, NULL,
        timeout_ms < 0 ? NULL : &tv);
    if (select_result == -1)
        if (errno == EINTR)
            return;
        else
            throw std::runtime_error("select() failed");

    for (std::vector<int>::const_iterator it = listen_sockets.begin();
            it != listen_sockets.end(); ++it)
        if (FD_ISSET(*it, &fs_read)) {
            int read_socket = accept(*it, NULL, NULL);
            if (read_socket == -1)
                throw std::runtime_error("accept() failed");
            Connection* connection = new Connection;
            try {
                read_sockets.insert(std::map<int, Connection*>::value_type(
                    read_socket,  connection));
            } catch (...) {
                delete connection;
                throw;
            }
        }

    for (std::map<int, Connection*>::iterator it = read_sockets.begin();
            it != read_sockets.end();) {
        int read_socket = it->first;

        if (FD_ISSET(read_socket, &fs_read)) {
            int read_result = read(read_socket, buffer, sizeof(buffer));
            if (read_result == -1)
                if (errno == ECONNRESET)
                    goto close_socket;
                else
                    throw std::runtime_error("read() on socket failed");
            if (read_result == 0)
                it->second->close_socket = true;
            else {
                it->second->input_buffer.append(buffer, read_result);
                process_connection_read(*it->second);
            }
        }

        if (!it->second->output_buffer.empty() &&
                FD_ISSET(read_socket, &fs_write)) {
            process_connection_write(*it->second);
            int write_result = write(read_socket,
                it->second->output_buffer.data(),
                it->second->output_buffer.size());
            if (write_result == -1)
                throw std::runtime_error("write() failed");
            it->second->output_buffer.erase(0, write_result);
        }

        if (it->second->close_socket && it->second->output_buffer.empty()) {
        close_socket:
            int close_result = close(it->first);
            if (close_result == -1 && errno != ECONNRESET)
                throw std::runtime_error("close() failed");
            Connection* connection = it->second;
            read_sockets.erase(it++);
            delete connection;
        } else
            ++it;
    }
}


void
FastCGIServer::process_forever()
{
    for (;;)
        process();
}


void
FastCGIServer::process_connection_read(Connection& connection)
{
    std::string::size_type n = 0;
    while (connection.input_buffer.size() - n >= FCGI_HEADER_LEN) {
        const FCGI_Header& header = *reinterpret_cast<const FCGI_Header*>(
            connection.input_buffer.data() + n);
        if (header.version != FCGI_VERSION_1) {
            connection.close_socket = true;
            break;
        }

        unsigned content_length =
            (header.contentLengthB1 << 8) + header.contentLengthB0;
        if (connection.input_buffer.size() - n <
                FCGI_HEADER_LEN + content_length + header.paddingLength)
            break;
        const char* content =
            connection.input_buffer.data() + n + FCGI_HEADER_LEN;

        RequestID request_id = (header.requestIdB1 << 8) + header.requestIdB0;

        switch (header.type) {
        case FCGI_GET_VALUES: {
            Pairs pairs = parse_pairs(content, content_length);

            std::string::size_type base = connection.output_buffer.size();
            connection.output_buffer.push_back(FCGI_VERSION_1);
            connection.output_buffer.push_back(FCGI_GET_VALUES_RESULT);
            connection.output_buffer.append(FCGI_HEADER_LEN - 2, 0);

            for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it)
                if (it->first == FCGI_MAX_CONNS)
                    write_pair(connection.output_buffer,
                        it->first, std::string("100"));
                else if (it->first == FCGI_MAX_REQS)
                    write_pair(connection.output_buffer,
                        it->first, std::string("1000"));
                else if (it->first == FCGI_MPXS_CONNS)
                    write_pair(connection.output_buffer,
                        it->first, std::string("1"));

            std::string::size_type len = connection.output_buffer.size() - base;
            connection.output_buffer[base + 4] = (len >> 8) & 0xff;
            connection.output_buffer[base + 5] = len & 0xff;
            break;
        }
        case FCGI_BEGIN_REQUEST: {
            if (content_length < sizeof(FCGI_BeginRequestBody))
                break;
            const FCGI_BeginRequestBody& body =
                *reinterpret_cast<const FCGI_BeginRequestBody*>(content);

            if (!(body.flags & FCGI_KEEP_CONN))
                connection.close_responsibility = true;

            unsigned role = (body.roleB1 << 8) + body.roleB0;
            if (role != FCGI_RESPONDER) {
                FCGI_EndRequestRecord unknown;
                bzero(&unknown, sizeof(unknown));
                unknown.header.version = FCGI_VERSION_1;
                unknown.header.type = FCGI_END_REQUEST;
                unknown.header.contentLengthB0 = sizeof(unknown.body);
                unknown.body.protocolStatus = FCGI_UNKNOWN_ROLE;
                connection.output_buffer.append(
                    reinterpret_cast<const char*>(&unknown), sizeof(unknown));
                if (connection.close_responsibility)
                    connection.close_socket = true;
                break;
            }

            {
                RequestList::iterator it = connection.requests.find(request_id);
                if (it != connection.requests.end()) {
                    delete it->second;
                    connection.requests.erase(it);
                }
            }

            RequestInfo* new_request = new RequestInfo;
            try {
                connection.requests.insert(RequestList::value_type(
                    request_id, new_request));
            } catch (...) {
                delete new_request;
                throw;
            }
            break;
        }
        case FCGI_ABORT_REQUEST: {
            RequestList::iterator it = connection.requests.find(request_id);
            if (it == connection.requests.end())
                break;

            FCGI_EndRequestRecord aborted;
            bzero(&aborted, sizeof(aborted));
            aborted.header.version = FCGI_VERSION_1;
            aborted.header.type = FCGI_END_REQUEST;
            aborted.header.contentLengthB0 = sizeof(aborted.body);
            aborted.body.appStatusB0 = 1;
            aborted.body.protocolStatus = FCGI_REQUEST_COMPLETE;
            connection.output_buffer.append(
                reinterpret_cast<const char*>(&aborted), sizeof(aborted));
            if (connection.close_responsibility)
                connection.close_socket = true;

            delete it->second;
            connection.requests.erase(it);
            break;
        }
        case FCGI_PARAMS: {
            RequestList::iterator it = connection.requests.find(request_id);
            if (it == connection.requests.end())
                break;

            RequestInfo& request = *it->second;
            if (!request.params_closed)
                if (content_length != 0)
                    request.params_buffer.append(content, content_length);
                else {
                    request.params = parse_pairs(request.params_buffer.data(),
                        request.params_buffer.size());
                    request.params_buffer.clear();
                    request.params_closed = true;

                    request.status = (*handle_request)(request);
                    if (request.status == 0 && !request.in.empty()) {
                        request.status = (*handle_data)(request);
                        if (request.status == 0 && request.in_closed)
                            request.status = (*handle_complete)(request);
                    }
                    process_write_request(connection, request_id, request);
                }
            break;
        }
        case FCGI_STDIN: {
            RequestList::iterator it = connection.requests.find(request_id);
            if (it == connection.requests.end())
                break;

            RequestInfo& request = *it->second;
            if (!request.in_closed)
                if (content_length != 0) {
                    request.in.append(content, content_length);
                    if (request.params_closed && request.status == 0) {
                        request.status = (*handle_data)(request);
                        process_write_request(connection, request_id, request);
                    }
                } else {
                    request.in_closed = true;
                    if (request.params_closed && request.status == 0) {
                        request.status = (*handle_complete)(request);
                        process_write_request(connection, request_id, request);
                    }
                }
            break;
        }
        case FCGI_DATA:
            break;
        default: {
            FCGI_UnknownTypeRecord unknown;
            bzero(&unknown, sizeof(unknown));
            unknown.header.version = FCGI_VERSION_1;
            unknown.header.type = FCGI_UNKNOWN_TYPE;
            unknown.header.contentLengthB0 = sizeof(unknown.body);
            unknown.body.type = header.type;
            connection.output_buffer.append(
                reinterpret_cast<const char*>(&unknown), sizeof(unknown));
        }
        }

        n += FCGI_HEADER_LEN + content_length + header.paddingLength;
    }

    connection.input_buffer.erase(0, n);
}


void
FastCGIServer::process_write_request(Connection& connection, RequestID id,
                                     RequestInfo& request)
{
    if (!request.out.empty()) {
        write_data(connection.output_buffer, id, request.out, FCGI_STDOUT);
        request.out.clear();
    }
    if (!request.err.empty()) {
        write_data(connection.output_buffer, id, request.err, FCGI_STDERR);
        request.err.clear();
    }
    if ((request.in_closed || request.status != 0) &&
            !request.output_closed) {
        write_data(connection.output_buffer, id, request.out, FCGI_STDOUT);
        write_data(connection.output_buffer, id, request.err, FCGI_STDERR);

        FCGI_EndRequestRecord complete;
        bzero(&complete, sizeof(complete));
        complete.header.version = FCGI_VERSION_1;
        complete.header.type = FCGI_END_REQUEST;
        complete.header.requestIdB1 = (id >> 8) & 0xff;
        complete.header.requestIdB0 = id & 0xff;
        complete.header.contentLengthB0 = sizeof(complete.body);
        complete.body.appStatusB3 = (request.status >> 24) & 0xff;
        complete.body.appStatusB2 = (request.status >> 16) & 0xff;
        complete.body.appStatusB1 = (request.status >> 8) & 0xff;
        complete.body.appStatusB0 = request.status & 0xff;
        complete.body.protocolStatus = FCGI_REQUEST_COMPLETE;
        connection.output_buffer.append(
            reinterpret_cast<const char*>(&complete), sizeof(complete));
        if (connection.close_responsibility)
            connection.close_socket = true;

        request.output_closed = true;
    }
}


void
FastCGIServer::process_connection_write(Connection& connection)
{
    for (RequestList::iterator it = connection.requests.begin();
            it != connection.requests.end();) {
        process_write_request(connection, it->first, *it->second);
        if (it->second->params_closed && it->second->in_closed) {
            RequestInfo* request = it->second;
            connection.requests.erase(it++);
            delete request;
        } else
            ++it;
    }
}


FastCGIServer::Pairs
FastCGIServer::parse_pairs(const char* data, std::string::size_type n)
{
    Pairs pairs;

    const unsigned char* u = reinterpret_cast<const unsigned char*>(data);

    for (std::string::size_type m = 0; m < n;) {
        std::string::size_type name_length, value_length;

        if (u[m] >> 7) {
            if (n - m < 4)
                break;
            name_length = ((u[m] & 0x7f) << 24) + (u[m + 1] << 16) +
                (u[m + 2] << 8) + u[m + 3];
            m += 4;
        } else
            name_length = u[m++];
        if (m >= n)
            break;

        if (u[m] >> 7) {
            if (n - m < 4)
                break;
            value_length = ((u[m] & 0x7f) << 24) + (u[m + 1] << 16) +
                (u[m + 2] << 8) + u[m + 3];
            m += 4;
        } else
            value_length = u[m++];

        if (n - m < name_length)
            break;
        std::string key(data + m, name_length);
        m += name_length;

        if (n - m < value_length)
            break;
        pairs.insert(Pairs::value_type(
            key, std::string(data + m, value_length)));
        m += value_length;
    }

    return pairs;
}


void
FastCGIServer::write_pair(std::string& buffer,
                          const std::string& key, const std::string& value)
{
    if (key.size() > 0x7f) {
        buffer.push_back(0x80 + ((key.size() >> 24) & 0x7f));
        buffer.push_back((key.size() >> 16) & 0xff);
        buffer.push_back((key.size() >> 8) & 0xff);
        buffer.push_back(key.size() & 0xff);
    } else
        buffer.push_back(key.size());

    if (value.size() > 0x7f) {
        buffer.push_back(0x80 + ((value.size() >> 24) & 0x7f));
        buffer.push_back((value.size() >> 16) & 0xff);
        buffer.push_back((value.size() >> 8) & 0xff);
        buffer.push_back(value.size() & 0xff);
    } else
        buffer.push_back(value.size());

    buffer.append(key);
    buffer.append(value);
}


void
FastCGIServer::write_data(std::string& buffer, RequestID id,
                          const std::string& input, unsigned char type)
{
    FCGI_Header header;
    bzero(&header, sizeof(header));
    header.version = FCGI_VERSION_1;
    header.type = type;
    header.requestIdB1 = (id >> 8) & 0xff;
    header.requestIdB0 = id & 0xff;

    for (std::string::size_type n = 0;;) {
        std::string::size_type written = std::min(input.size() - n,
            (std::string::size_type)0xffffu);

        header.contentLengthB1 = written >> 8;
        header.contentLengthB0 = written & 0xff;
        header.paddingLength = (8 - (written % 8)) % 8;
        buffer.append(
            reinterpret_cast<const char*>(&header), sizeof(header));
        buffer.append(input.data() + n, written);
        buffer.append(header.paddingLength, 0);

        n += written;
        if (n == input.size())
            break;
    }
}
