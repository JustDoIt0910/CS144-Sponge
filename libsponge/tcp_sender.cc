#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{std::random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _rto(_initial_retransmission_timeout)
    , _stream(capacity)
    , _inflight_segments() {}

uint64_t TCPSender::bytes_in_flight() const { return _inflight_bytes; }

void TCPSender::fill_window() {
    if(_fin_sent)
        return;
    size_t window_size = std::max(_window_size, static_cast<uint16_t>(1));
    while(_inflight_bytes < window_size) {
        size_t window_remain = window_size - _inflight_bytes;
        size_t bytes_can_send = std::min(window_remain, TCPConfig::MAX_PAYLOAD_SIZE);
        TCPSegment segment;
        TCPHeader& header = segment.header();
        if(!_syn_sent) {
            header.syn = true;
            _syn_sent = true;
            bytes_can_send -= 1;
        }
        segment.payload() = Buffer(_stream.read(bytes_can_send));
        if(_stream.eof() && segment.payload().size() < window_remain) {
            header.fin = true;
            _fin_sent = true;
        }
        if(segment.length_in_sequence_space() == 0)
            break;
        if(_inflight_segments.empty()) {
            _rto = _initial_retransmission_timeout;
            _time = 0;
        }
        header.seqno = wrap(_next_seqno, _isn);
        _segments_out.push(segment);
        InflightSegment inflight(_next_seqno, segment);
        _inflight_segments.push_back(inflight);
        _next_seqno += segment.length_in_sequence_space();
        _inflight_bytes += segment.length_in_sequence_space();
        if(_fin_sent)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ack = unwrap(ackno, _isn, _abs_ack);
    if(abs_ack > _next_seqno)
        return;
    _abs_ack = abs_ack;
    bool ack_success = false;
    for(auto it = _inflight_segments.begin(); it != _inflight_segments.end(); ) {
        if(it->_abs_seq + it->_segment.length_in_sequence_space() <= _abs_ack) {
            _inflight_bytes -= it->_segment.length_in_sequence_space();
            it = _inflight_segments.erase(it);
            ack_success = true;
        }
        else
            break;
    }
    if(ack_success) {
        _rto = _initial_retransmission_timeout;
        _consecutive_retransmission = 0;
        _time = 0;
    }
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _time += ms_since_last_tick;
    if(_time >= _rto && !_inflight_segments.empty()) {
        InflightSegment earliest = _inflight_segments.front();
        _segments_out.push(earliest._segment);
        if(_window_size > 0) {
            _consecutive_retransmission++;
            _rto *= 2;
        }
        _time = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmission; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}
