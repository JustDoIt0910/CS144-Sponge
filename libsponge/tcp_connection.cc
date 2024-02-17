#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}


size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received = 0;
    const TCPHeader& header = seg.header();
    if(header.rst) {
        set_error();
        return;
    }
    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        if(!header.syn)
            return;
        _receiver.segment_received(seg);
        connect();
        return;
    }
    bool occupied_seq = seg.length_in_sequence_space() > 0;
    if(occupied_seq)
        _receiver.segment_received(seg);
    if(header.ack)
        _sender.ack_received(header.ackno, header.win);

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    if(TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _active = false;
        return;
    }

    if(occupied_seq && _sender.segments_out().empty())
        _sender.send_empty_segment();
    real_send();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const std::string &data) {
    size_t written = _sender.stream_in().write(data);
    _sender.fill_window();
    real_send();
    return written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _time_since_last_segment_received += ms_since_last_tick;
    if(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        std::queue<TCPSegment>().swap(_sender.segments_out());
        send_rst();
        set_error();
        return;
    }
    real_send();
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish &&
        _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        _active = false;
        _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    real_send();
}

void TCPConnection::connect() {
    _sender.fill_window();
    real_send();
    _active = true;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            std::cerr << "Warning: Unclean shutdown of TCPConnection\n";
            send_rst();
            set_error();
        }
    } catch (const std::exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::real_send() {
    std::queue<TCPSegment>& out = _sender.segments_out();
    while(!out.empty()) {
        TCPSegment& segment = out.front();
        if(_receiver.ackno()) {
            segment.header().ack = true;
            segment.header().ackno = *_receiver.ackno();
        }
        segment.header().win = _receiver.window_size();
        _segments_out.push(segment);
        out.pop();
    }
}

void TCPConnection::set_error() {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _linger_after_streams_finish = false;
    _active = false;
}

void TCPConnection::send_rst() {
    TCPSegment rst;
    rst.header().rst = true;
    _segments_out.push(rst);
}