#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader& header = seg.header();
    WrappingInt32 seq = header.seqno;
    if(header.syn) {
        _isn = seq;
        _syn_recv = true;
    }
    if(!_syn_recv)
        return;
    uint64_t index = unwrap(seq, _isn, _assembled_idx);
    if(!header.syn)
        index -= 1;
    _reassembler.push_substring(seg.payload().copy(), index, header.fin);
    _ackno = WrappingInt32(_isn.raw_value() + _reassembler.stream_out().bytes_written() + 1 +
                           _reassembler.stream_out().input_ended());
    _assembled_idx = _reassembler.stream_out().bytes_written() + 1;
}

std::optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const {
    return _capacity - _reassembler.stream_out().buffer_size();
}
