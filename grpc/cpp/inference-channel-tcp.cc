/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// Copyright (c) 2026 Muhammad Uzair
// SPDX-License-Identifier: GPL-2.0-only

#include "inference-channel-tcp.h"

namespace ns3
{
namespace oranntn
{
namespace airan
{

TcpInferenceChannel::TcpInferenceChannel() = default;

TcpInferenceChannel::TcpInferenceChannel(SystemSocket::Handle fd)
    : m_fd(fd)
{
}

TcpInferenceChannel::~TcpInferenceChannel()
{
    Close();
}

bool
TcpInferenceChannel::ConnectTo(const std::string& host, uint16_t port)
{
    Close();
    m_fd = SystemSocket::Connect(host, port);
    return m_fd >= 0;
}

bool
TcpInferenceChannel::Send(const std::vector<uint8_t>& bytes)
{
    if (m_fd < 0)
    {
        return false;
    }
    const auto length = static_cast<uint32_t>(bytes.size());
    std::vector<uint8_t> frame = {static_cast<uint8_t>(length >> 24),
                                  static_cast<uint8_t>(length >> 16),
                                  static_cast<uint8_t>(length >> 8),
                                  static_cast<uint8_t>(length)};
    frame.insert(frame.end(), bytes.begin(), bytes.end());
    size_t offset = 0;
    while (offset < frame.size())
    {
        const auto n = SystemSocket::Send(m_fd, frame.data() + offset, frame.size() - offset);
        if (n <= 0)
        {
            Close();
            return false;
        }
        offset += static_cast<size_t>(n);
    }
    m_bytesSent.fetch_add(bytes.size());
    m_framesSent.fetch_add(1);
    return true;
}

bool
TcpInferenceChannel::RecvExact(uint8_t* out, size_t n, uint32_t timeout_ms)
{
    size_t got = 0;
    while (got < n)
    {
        if (!SystemSocket::WaitReadable(m_fd, timeout_ms))
        {
            return false;
        }
        const auto r = SystemSocket::Receive(m_fd, out + got, n - got);
        if (r <= 0)
        {
            Close();
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

bool
TcpInferenceChannel::TryRecv(std::vector<uint8_t>& bytes, uint32_t timeout_ms)
{
    if (m_fd < 0)
    {
        return false;
    }
    uint8_t header[4]{};
    if (!RecvExact(header, sizeof(header), timeout_ms))
    {
        return false;
    }
    const uint32_t len = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) |
                         (uint32_t(header[2]) << 8) | header[3];
    if (len == 0 || len > (16 * 1024 * 1024))
    {
        Close();
        return false;
    }
    bytes.assign(len, 0);
    if (!RecvExact(bytes.data(), len, timeout_ms))
    {
        return false;
    }
    m_bytesRecv.fetch_add(len);
    m_framesRecv.fetch_add(1);
    return true;
}

bool
TcpInferenceChannel::IsOpen() const
{
    return m_fd >= 0;
}

void
TcpInferenceChannel::Close()
{
    if (m_fd >= 0)
    {
        SystemSocket::Close(m_fd);
        m_fd = -1;
    }
}

// ----------------------------------------------------------------------------
// TcpInferenceListener
// ----------------------------------------------------------------------------

TcpInferenceListener::TcpInferenceListener() = default;

TcpInferenceListener::~TcpInferenceListener()
{
    Close();
}

bool
TcpInferenceListener::Listen(const std::string& host, uint16_t port, int backlog)
{
    Close();
    m_fd = SystemSocket::Listen(host, port, backlog);
    m_port = SystemSocket::GetLocalPort(m_fd);
    return m_fd >= 0;
}

std::unique_ptr<TcpInferenceChannel>
TcpInferenceListener::AcceptOne(uint32_t timeout_ms)
{
    const auto fd = SystemSocket::Accept(m_fd, timeout_ms);
    return fd < 0 ? nullptr : std::make_unique<TcpInferenceChannel>(fd);
}

void
TcpInferenceListener::Close()
{
    if (m_fd >= 0)
    {
        SystemSocket::Close(m_fd);
        m_fd = -1;
    }
    m_port = 0;
}

} // namespace airan
} // namespace oranntn
} // namespace ns3
