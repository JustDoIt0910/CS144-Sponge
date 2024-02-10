#include "stream_reassembler.hh"

#include <assert.h>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

StreamReassembler::StreamReassembler(const size_t capacity) :
    _first_unassembled(0),
    _unassembled_segments(),
    _unassembled_bytes(0),
    _eof_recv(false),
    _output(capacity),
    _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const std::string &data, const size_t index, const bool eof) {
    bool eof_ignore =  false;
    size_t seg_start = (index < _first_unassembled) ? _first_unassembled : index;
    size_t seg_end = index + data.size();
    if(seg_end <= seg_start) {
        handle_eof(eof);
        return;
    }
    auto it = _unassembled_segments.upper_bound(seg_start);
    if(it != _unassembled_segments.begin()) {
        auto before = std::prev(it);
        if(before != _unassembled_segments.end()) {
            size_t before_end = before->first + before->second.size();
            if(seg_start < before_end) {
                if(seg_end <= before_end) {
                    handle_eof(eof);
                    return;
                }
                seg_start = before_end;
            }
        }
    }
    while(it != _unassembled_segments.end()) {
        if(it->first >= seg_end)
            break;
        size_t after_end = it->first + it->second.size();
        if(after_end <= seg_end) {
            _unassembled_bytes -= it->second.size();
            it = _unassembled_segments.erase(it);
            continue;
        }
        seg_end = it->first;
        if(seg_end == seg_start) {
            handle_eof(eof);
            return;
        }
        break;
    }
    size_t first_unacceptable = _capacity - _output.buffer_size() + _first_unassembled;
    if(seg_start >= first_unacceptable)
        return;
    if(seg_end > first_unacceptable) {
        eof_ignore = true;
        seg_end = first_unacceptable;
    }
    std::string segment = data.substr(seg_start - index, seg_end - seg_start);
    if(seg_start == _first_unassembled) {
        size_t written = _output.write(segment);
        _first_unassembled += written;
        if(written < segment.size()) {
            std::string remain = segment.substr(written);
            _unassembled_bytes += remain.size();
            _unassembled_segments.emplace(seg_start + written, std::move(remain));
        }
    }
    else {
        _unassembled_bytes += segment.size();
        _unassembled_segments.emplace(seg_start, std::move(segment));
    }
    for(auto iter = _unassembled_segments.begin(); iter != _unassembled_segments.end();) {
        assert(iter->first >= _first_unassembled);
        if (iter->first == _first_unassembled) {
            const size_t written = _output.write(iter->second);
            _first_unassembled += written;
            _unassembled_bytes -= written;
            if (written < iter->second.size()) {
                iter->second = iter->second.substr(written);
                break;
            }
            iter = _unassembled_segments.erase(iter);
            continue;
        }
        break;
    }
    if(!eof_ignore)
        handle_eof(eof);
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }

void StreamReassembler::handle_eof(const bool eof) {
    if(eof)
        _eof_recv = true;
    if(_eof_recv && _unassembled_bytes == 0)
        _output.end_input();
}